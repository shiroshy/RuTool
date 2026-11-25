#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QCryptographicHash>
#include <QtEndian>
#include <rutool/platform/Runtime.hpp>
#include <rutool/platform/FileOps.hpp>
#include <rutool/platform/Crypto.hpp>
#include <rutool/security/Signatures.hpp>
#include <rutool/core/Installation.hpp>
#include <rutool/core/InstallerConfig.hpp>
#include <rutool/network/Transfers.hpp>
#include <rutool/network/ServiceClient.hpp>
#include <rutool/network/Premium.hpp>
#include "PortableCrypto.hpp"
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <cerrno>
#include <QNetworkReply>
#include <QTimer>
#include <cstring>
#include <unistd.h>

// Fault injection for the durability boundary after a successful rename.
// The production executable never links this test override.
namespace {
thread_local const char* syncFailureTarget{};
thread_local bool corruptAfterRename{};
thread_local bool crashAfterRename{};
}
extern "C" int fsync(int fd) {
    if (syncFailureTarget) {
        struct stat info{};
        if (::fstat(fd, &info) == 0 && S_ISDIR(info.st_mode)) {
            const int targetFd = ::open(syncFailureTarget, O_RDONLY | O_CLOEXEC);
            char prefix[8]{};
            const auto count = targetFd >= 0 ? ::read(targetFd, prefix, sizeof(prefix)) : -1;
            if (targetFd >= 0) ::close(targetFd);
            if (count == 8 && std::memcmp(prefix, "translat", 8) == 0) {
                if (crashAfterRename) ::_exit(77);
                if (corruptAfterRename) {
                    const int changed = ::open(syncFailureTarget, O_WRONLY | O_TRUNC | O_CLOEXEC);
                    if (changed >= 0) {
                        const auto written = ::write(changed, "unknown contents", 16);
                        ::close(changed);
                        if (written != 16) return -1;
                    }
                }
                syncFailureTarget = nullptr; errno = EIO; return -1;
            }
        }
    }
    return static_cast<int>(::syscall(SYS_fsync, fd));
}
using namespace rutool;
namespace {
QByteArray read(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
bool write(const QString& path, const QByteArray& bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
QString fixture(const char* name) { return QDir(QStringLiteral(RUTOOL_LINUX_FIXTURES)).filePath(QString::fromLatin1(name)); }
crypto::Bytes view(const QByteArray& bytes) { return {reinterpret_cast<const unsigned char*>(bytes.constData()), static_cast<size_t>(bytes.size())}; }
}
class FixtureReply final : public QNetworkReply {
    QByteArray body_;
    qint64 offset_{};
public:
    FixtureReply(const QNetworkRequest& request, QByteArray body, int status, bool badRange, QObject* parent)
        : QNetworkReply(parent), body_(std::move(body)) {
        setRequest(request); setUrl(request.url()); open(QIODevice::ReadOnly);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setHeader(QNetworkRequest::ContentLengthHeader, body_.size());
        if (status == 302) setAttribute(QNetworkRequest::RedirectionTargetAttribute, QUrl(QStringLiteral("https://evil.invalid/file")));
        if (status == 206) setRawHeader("Content-Range", badRange ? QByteArray("invalid") : QByteArray("bytes 3-") + QByteArray::number(body_.size() + 2) + '/' + QByteArray::number(body_.size() + 3));
        QTimer::singleShot(0, this, [this] {
            emit metaDataChanged();
            if (isFinished()) return;
            emit readyRead();
            if (isFinished()) return;
            setFinished(true); emit finished();
        });
    }
    void abort() override { if (!isFinished()) { setError(OperationCanceledError, QStringLiteral("aborted")); setFinished(true); emit finished(); } }
    qint64 bytesAvailable() const override { return body_.size() - offset_ + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char* data, qint64 maximum) override {
        const auto count = std::min(maximum, static_cast<qint64>(body_.size()) - offset_);
        if (count <= 0) return -1;
        std::memcpy(data, body_.constData() + offset_, static_cast<size_t>(count)); offset_ += count; return count;
    }
};
class FixtureTransport final : public QNetworkAccessManager {
public:
    QByteArray payload{"download fixture bytes"};
    QByteArray authorization;
    int requests{};
    int status{200}; bool badRange{};
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
        ++requests;
        authorization = request.rawHeader("Authorization");
        return new FixtureReply(request, status == 206 ? payload.mid(3) : payload, status, badRange, this);
    }
};
class LinuxTests : public QObject {
    Q_OBJECT
private slots:
    void installerConfiguration() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto previous = qgetenv("XDG_CONFIG_HOME");
        const auto restore = qScopeGuard([&] {
            if (previous.isNull()) qunsetenv("XDG_CONFIG_HOME");
            else qputenv("XDG_CONFIG_HOME", previous);
        });
        qputenv("XDG_CONFIG_HOME", temp.path().toUtf8());
        const auto name = QStringLiteral("installer-keys.json");
        const auto bundled = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("app/config/") + name);
        QCOMPARE(installerConfigPath(name), bundled);
        QVERIFY(readPackagePassword(bundled));
    }

    void cryptoVectors() {
        QByteArray output(32, '\0');
        QVERIFY(crypto::pbkdf2Sha256(view(QByteArray("password")), view(QByteArray("salt")), 1,
            {reinterpret_cast<unsigned char*>(output.data()), 32}));
        QCOMPARE(output.toHex(), QByteArray("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"));
        // NIST AES-256-GCM, empty plaintext, all-zero key and 96-bit IV.
        platform::Secret key(QByteArray(32, '\0'));
        auto decrypted = platform::decryptGcm(key, QByteArray(12, '\0'), {}, QByteArray::fromHex("530f8afbc74536b9a963b4f1c4cb738b"), {});
        QVERIFY(decrypted); QVERIFY(decrypted->bytes().isEmpty());
        QVERIFY(!platform::decryptGcm(key, QByteArray(12, '\0'), {}, QByteArray(16, '\0'), {}));
        const auto message = read(fixture("message.bin")), signature = read(fixture("signature.p1363")), publicKey = read(fixture("public.der"));
        QVERIFY(verifySignedPayload(message, signature, publicKey));
        QVERIFY(!verifySignedPayload(message + '!', signature, publicKey));
        QVERIFY(!verifySignedPayload(message, signature.left(63), publicKey));
    }
    void localProtection() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); qputenv("XDG_DATA_HOME", temp.path().toUtf8());
        auto cipher = platform::protectUser("secret fixture", "context"); QVERIFY(cipher);
        auto again = platform::protectUser("secret fixture", "context"); QVERIFY(again); QVERIFY(*cipher != *again);
        auto plain = platform::unprotectUser(*cipher, "context"); QVERIFY(plain); QCOMPARE(plain->bytes(), QByteArray("secret fixture"));
        QVERIFY(!platform::unprotectUser(*cipher, "other context"));
        auto tampered = *cipher; tampered[tampered.size() - 1] ^= 1;
        QVERIFY(!platform::unprotectUser(tampered, "context"));
        const auto path = QDir(temp.path()).filePath(QStringLiteral("RuToolInstaller/private/local.key"));
        struct stat st{}; QVERIFY(::stat(QFile::encodeName(path).constData(), &st) == 0); QCOMPARE(st.st_mode & 0777, mode_t{0600});
        QVERIFY(::chmod(QFile::encodeName(path).constData(), 0644) == 0);
        QVERIFY(!platform::unprotectUser(*cipher, "context"));
        QVERIFY(::chmod(QFile::encodeName(path).constData(), 0600) == 0);
        const auto originalKey = read(path);
        const auto alias = QFile::encodeName(path + QStringLiteral(".alias"));
        QVERIFY(::link(QFile::encodeName(path).constData(), alias.constData()) == 0);
        QVERIFY(!platform::protectUser("new data", "context"));
        QCOMPARE(read(path), originalKey);
        QVERIFY(QFile::remove(QString::fromLocal8Bit(alias)));
        QVERIFY(write(path, "damaged key"));
        QVERIFY(!platform::protectUser("new data", "context"));
        QCOMPARE(read(path), QByteArray("damaged key"));
        QVERIFY(write(path, originalKey));
        QVERIFY(QFile::remove(path)); QVERIFY(!platform::unprotectUser(*cipher, "context"));
        const auto id = platform::installationDeviceId(); QVERIFY(id); QCOMPARE(id->size(), qsizetype{64});
    }
    void pathsLocksAndReplacement() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); const QDir dir(temp.path());
        const auto target = dir.filePath(QStringLiteral("target")), staged = dir.filePath(QStringLiteral("stage"));
        QVERIFY(write(target, "old")); QVERIFY(write(staged, "new"));
        QVERIFY(platform::requireSafeFile(target));
        QVERIFY(!platform::requireSafeFile(target + QChar::Null + QStringLiteral("hidden")));
        QVERIFY(::chmod(QFile::encodeName(target).constData(), 0640) == 0);
        QVERIFY(QFile::link(target, dir.filePath(QStringLiteral("link")))); QVERIFY(!platform::requireSafeFile(dir.filePath(QStringLiteral("link"))));
        QVERIFY(QFile::link(temp.path(), dir.filePath(QStringLiteral("dirlink"))));
        QVERIFY(!platform::requireSafeFile(dir.filePath(QStringLiteral("dirlink/target"))));
        const auto hard = QFile::encodeName(dir.filePath(QStringLiteral("hard")));
        QVERIFY(::link(QFile::encodeName(target).constData(), hard.constData()) == 0); QVERIFY(!platform::requireSafeFile(target)); QVERIFY(QFile::remove(QString::fromLocal8Bit(hard)));
        auto first = platform::FileLock::operation(target); QVERIFY(first); QVERIFY(!platform::FileLock::operation(target));
        first->reset(); QVERIFY(platform::FileLock::operation(target));
        QProcess peer;
        peer.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--lock-process-fixture"), target});
        QVERIFY(peer.waitForStarted()); QVERIFY(peer.waitForReadyRead());
        const bool peerBlocked = !platform::FileLock::operation(target);
        peer.kill(); QVERIFY(peer.waitForFinished()); QVERIFY(peerBlocked);
        QVERIFY(platform::FileLock::operation(target));
        auto cache = platform::FileLock::cacheEntry(dir.filePath(QStringLiteral("cache.rut"))); QVERIFY(cache); QVERIFY(!platform::FileLock::cacheEntry(dir.filePath(QStringLiteral("cache.rut"))));
        auto lease = platform::FileLock::directoryLease(temp.path()); QVERIFY(lease); QVERIFY(!platform::FileLock::directoryLease(temp.path()));
        QVERIFY(platform::flushFile(staged)); QVERIFY(platform::replaceFile(staged, target)); QCOMPARE(read(target), QByteArray("new")); QVERIFY(!QFileInfo::exists(staged));
        struct stat targetMode{}; QVERIFY(::stat(QFile::encodeName(target).constData(), &targetMode) == 0);
        QCOMPARE(targetMode.st_mode & 0777, mode_t{0640});
        QVERIFY(!platform::replaceFile(dir.filePath(QStringLiteral("link")), target));
        QVERIFY(platform::requireFreeSpace({{target, 1}})); QVERIFY(!platform::requireFreeSpace({{target, std::numeric_limits<quint64>::max()}}));
        QVERIFY(platform::currentProcessStartedTicks());
    }
    void steamDiscovery() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); const QDir dir(temp.path());
        const auto steam = dir.filePath(QStringLiteral("Steam")), library = dir.filePath(QStringLiteral("Library With Spaces"));
        const auto game = QDir(library).filePath(QStringLiteral("steamapps/common/Blue Archive"));
        QVERIFY(write(gameDatabasePath(game), "fixture"));
        QVERIFY(write(QDir(steam).filePath(QStringLiteral("steamapps/libraryfolders.vdf")), QByteArray("\"libraryfolders\" { \"0\" { \"path\" \"") + library.toUtf8() + "\" } }"));
        QVERIFY(write(QDir(library).filePath(QStringLiteral("steamapps/appmanifest_123.acf")), "\"AppState\" { \"installdir\" \"Blue Archive\" }"));
        QCOMPARE(platform::discoverGameInstallations({steam}), QStringList{game});
        const auto proton = QDir(steam).filePath(QStringLiteral("steamapps/compatdata/456/pfx/drive_c/Games/BlueArchive"));
        QVERIFY(write(gameDatabasePath(proton), "fixture"));
        const auto found = platform::discoverGameInstallations({steam}); QCOMPARE(found.size(), qsizetype{2}); QVERIFY(found.contains(proton));
        QVERIFY(platform::pathKey(QStringLiteral("A")) != platform::pathKey(QStringLiteral("a")));
    }
    void processGuard() {
        QVERIFY(platform::requireGameClosed());
        QProcess game;
        game.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--game-process-fixture")});
        QVERIFY(game.waitForStarted()); QVERIFY(game.waitForReadyRead());
        const bool detected = !platform::requireGameClosed(); game.kill(); game.waitForFinished(); QVERIFY(detected);
    }
    void packageInstallRestore() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); qputenv("XDG_DATA_HOME", temp.path().toUtf8()); const QDir dir(temp.path());
        const auto prepared = dir.filePath(QStringLiteral("prepared")); QVERIFY(QDir().mkpath(prepared));
        platform::Secret password(QByteArray("public-test-password"));
        auto package = decryptPackage(fixture("sample.rut.fixture"), prepared, password); QVERIFY2(package, package ? "" : qPrintable(package.error().message));
        const auto s = package->signature;
        RutSignature signature{1, s.value(QStringLiteral("FileSha256")).toString(), s.value(QStringLiteral("RuToolVersion")).toString(),
            s.value(QStringLiteral("WatermarkSha256")).toString(), s.value(QStringLiteral("Algorithm")).toString(),
            s.value(QStringLiteral("SignerId")).toString(), s.value(QStringLiteral("SignatureBase64")).toString()};
        QVERIFY(verifyRutSignature(signature, read(fixture("public.der"))));
        const auto game = dir.filePath(QStringLiteral("game")), database = gameDatabasePath(game);
        QVERIFY(write(database, read(fixture("original.db.fixture"))));
        InstallationRequest request; request.game = game; request.package = *package;
        request.backupRoot = dir.filePath(QStringLiteral("backups")); request.originalVerified = true;
        const auto deny = []() -> Result<void> { return std::unexpected(Error{ErrorCode::Signature, QStringLiteral("test guard rejected")}); };
        QVERIFY(!installPackage(request, deny)); QCOMPARE(read(database), read(fixture("original.db.fixture")));
        const auto guard = [&]() -> Result<void> {
            if (!verifyRutSignature(signature, read(fixture("public.der")))) return std::unexpected(Error{ErrorCode::Signature, QStringLiteral("test signature rejected")});
            return {};
        };
        auto installed = installPackage(request, guard); QVERIFY2(installed, installed ? "" : qPrintable(installed.error().message));
        QCOMPARE(read(database), read(fixture("patched.db.fixture"))); QVERIFY(!installed->originalBackup.isEmpty());
        auto receipt = originalReceipt(game, installed->originalBackup); QVERIFY(receipt);
        auto duplicate = installPackage(request, guard); QVERIFY(duplicate); QVERIFY(duplicate->alreadyInstalled);
        auto restored = restoreOriginal(game, installed->originalBackup); QVERIFY2(restored, restored ? "" : qPrintable(restored.error().message));
        QCOMPARE(read(database), read(fixture("original.db.fixture")));
        const auto encodedTarget = QFile::encodeName(database);
        syncFailureTarget = encodedTarget.constData();
        auto durabilityFailure = installPackage(request, guard);
        const bool injected = syncFailureTarget == nullptr; syncFailureTarget = nullptr;
        QVERIFY(injected); QVERIFY(!durabilityFailure);
        QCOMPARE(read(database), read(fixture("original.db.fixture")));
        QVERIFY(!QFileInfo::exists(database + QStringLiteral(".baretool-transaction.dpapi")));
        // Unknown bytes after a failed replacement must keep recovery data.
        syncFailureTarget = encodedTarget.constData(); corruptAfterRename = true;
        auto unknownFailure = installPackage(request, guard);
        const bool unknownInjected = syncFailureTarget == nullptr;
        syncFailureTarget = nullptr; corruptAfterRename = false;
        QVERIFY(unknownInjected); QVERIFY(!unknownFailure);
        const auto journal = database + QStringLiteral(".baretool-transaction.dpapi");
        QVERIFY(QFileInfo::exists(journal));
        QVERIFY(!recoverInstallation(game));
        QVERIFY(QFileInfo::exists(journal));
        bool originalPreserved{};
        for (const auto& backup : QDir(request.backupRoot).entryList({QStringLiteral("*.db")}, QDir::Files))
            originalPreserved = originalPreserved || read(QDir(request.backupRoot).filePath(backup)) == read(fixture("original.db.fixture"));
        QVERIFY(originalPreserved);
        QVERIFY(write(database, read(fixture("patched.db.fixture"))));
        QVERIFY(recoverInstallation(game));
        QCOMPARE(read(database), read(fixture("original.db.fixture")));
        QVERIFY(!QFileInfo::exists(journal));
        QVERIFY(write(dir.filePath(QStringLiteral("bad.rut")), read(fixture("sample.rut.fixture")).left(60)));
        const auto badFolder = dir.filePath(QStringLiteral("bad")); QVERIFY(QDir().mkpath(badFolder));
        QVERIFY(!decryptPackage(dir.filePath(QStringLiteral("bad.rut")), badFolder, password));
        auto corrupt = read(fixture("sample.rut.fixture")); corrupt[50] ^= 1; QVERIFY(write(dir.filePath(QStringLiteral("bad.rut")), corrupt));
        QVERIFY(!decryptPackage(dir.filePath(QStringLiteral("bad.rut")), badFolder, password));
        platform::Secret wrong(QByteArray("wrong password")); QVERIFY(!decryptPackage(fixture("sample.rut.fixture"), badFolder, wrong));
        auto cancelled = std::make_shared<std::atomic_bool>(true); QVERIFY(!decryptPackage(fixture("sample.rut.fixture"), badFolder, password, cancelled));
    }
    void abruptInstallerExit() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        qputenv("XDG_DATA_HOME", temp.path().toUtf8());
        const auto game = temp.filePath(QStringLiteral("game"));
        const auto database = gameDatabasePath(game);
        QVERIFY(write(database, read(fixture("original.db.fixture"))));
        QProcess installer;
        installer.start(QCoreApplication::applicationFilePath(),
            {QStringLiteral("--install-crash-fixture"), game, temp.filePath(QStringLiteral("backups"))});
        QVERIFY(installer.waitForFinished(15000));
        QCOMPARE(installer.exitCode(), 77);
        QCOMPARE(read(database), read(fixture("patched.db.fixture")));
        const auto journal = database + QStringLiteral(".baretool-transaction.dpapi");
        QVERIFY(QFileInfo::exists(journal));
        QVERIFY(recoverInstallation(game));
        QCOMPARE(read(database), read(fixture("original.db.fixture")));
        QVERIFY(!QFileInfo::exists(journal));
    }
    void downloads() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); QDir dir(temp.path()); FixtureTransport transport;
        const auto hash = QString::fromLatin1(QCryptographicHash::hash(transport.payload, QCryptographicHash::Sha256).toHex());
        const QUrl url(QStringLiteral("https://bluehalo.site/test-fixture"));
        const auto destination = dir.filePath(QStringLiteral("download.rut"));
        auto downloaded = downloadAsset(url, transport.payload.size(), hash, destination, {}, {}, &transport);
        QVERIFY(downloaded); QCOMPARE(read(destination), transport.payload);
        QVERIFY(QFile::remove(destination));
        QVERIFY(!downloadAsset(url, transport.payload.size(), QString(64, '0'), destination, {}, {}, &transport));
        QVERIFY(!QFileInfo::exists(destination)); QVERIFY(!QFileInfo::exists(destination + QStringLiteral(".partial")));
        QVERIFY(write(destination + QStringLiteral(".partial"), transport.payload.left(3))); transport.status = 206;
        QVERIFY(downloadAsset(url, transport.payload.size(), hash, destination, {}, {}, &transport));
        QCOMPARE(read(destination), transport.payload); QVERIFY(QFile::remove(destination));
        QVERIFY(write(destination + QStringLiteral(".partial"), transport.payload.left(3))); transport.badRange = true;
        QVERIFY(!downloadAsset(url, transport.payload.size(), hash, destination, {}, {}, &transport));
        transport.status = 302;
        QVERIFY(!downloadAsset(url, transport.payload.size(), hash, destination, {}, {}, &transport));
        QVERIFY(transport.authorization.isEmpty());
    }
    void addonDelta() {
        const QByteArray original("0123456789");
        const auto word = [](quint32 value) {
            QByteArray bytes(4, '\0'); qToLittleEndian(value, bytes.data()); return bytes;
        };
        const QByteArray magic("RUTD1\0", 6);
        const auto delta = magic + word(3) + word(2) + word(0x80000002u) + "XY" + word(5) + word(0);
        auto result = applyAddonDelta(original, delta);
        QVERIFY(result); QCOMPARE(*result, QByteArray("234XY01234"));
        QVERIFY(!applyAddonDelta({}, delta));
        QVERIFY(!applyAddonDelta(original, magic));
        QVERIFY(!applyAddonDelta(original, QByteArray("RUTD2\0", 6) + delta.sliced(6)));
        QVERIFY(!applyAddonDelta(original, magic + word(0) + word(0)));
        QVERIFY(!applyAddonDelta(original, magic + word(11) + word(0)));
        QVERIFY(!applyAddonDelta(original, magic + word(10) + word(1)));
        QVERIFY(!applyAddonDelta(original, magic + word(0xffffffffu)));
        QVERIFY(!applyAddonDelta(original, magic + word(1) + word(0xffffffffu)));
        for (qsizetype i = 0; i < delta.size(); ++i) QVERIFY(!applyAddonDelta(original, delta.first(i)));
        QVERIFY(!applyAddonDelta(original, delta + 'x'));
        QVERIFY(!applyAddonDelta(original, delta + word(1) + word(0)));
        const QByteArray maximum(64 * 1024 * 1024, 'x');
        QVERIFY(applyAddonDelta(maximum, magic + word(static_cast<quint32>(maximum.size())) + word(0)));
        QVERIFY(!applyAddonDelta(maximum + 'x', magic));
        QVERIFY(!applyAddonDelta(original, maximum + 'x'));
    }
    void addonDeltaGeneratorRoundtrip() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        QByteArray original(8192, '\0');
        for (qsizetype i = 0; i < original.size(); ++i) original[i] = static_cast<char>('a' + i % 23);
        auto localized = original; localized.replace(135, 5, "HELLO"); localized.replace(8050, 5, "WORLD");
        const auto source = temp.filePath(QStringLiteral("original.bundle"));
        const auto target = temp.filePath(QStringLiteral("localized.bundle"));
        const auto delta = temp.filePath(QStringLiteral("addon.rudp"));
        QVERIFY(write(source, original)); QVERIFY(write(target, localized));
        QProcess generator;
        generator.start(QStringLiteral(RUTOOL_PYTHON), {QStringLiteral(RUTOOL_DELTA_GENERATOR), source, target, delta});
        QVERIFY(generator.waitForFinished(10000)); QCOMPARE(generator.exitCode(), 0);
        const auto bytes = read(delta); QVERIFY(bytes.size() < localized.size());
        auto applied = applyAddonDelta(original, bytes); QVERIFY(applied); QCOMPARE(*applied, localized);
        const auto metadata = QJsonDocument::fromJson(generator.readAllStandardOutput()).object();
        QCOMPARE(metadata.value(QStringLiteral("packageBytes")).toInteger(), bytes.size());
        QCOMPARE(metadata.value(QStringLiteral("packageSha256")).toString(), *hashFile(delta));
        generator.start(QStringLiteral(RUTOOL_PYTHON), {QStringLiteral(RUTOOL_DELTA_GENERATOR), source, target, delta});
        QVERIFY(generator.waitForFinished(10000)); QVERIFY(generator.exitCode() != 0);
        QCOMPARE(read(delta), bytes); // The producer must refuse to overwrite existing output.
    }
    void premiumAddonCatalogAndTransfer() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); FixtureTransport transport;
        const auto hash = QString::fromLatin1(QCryptographicHash::hash(transport.payload, QCryptographicHash::Sha256).toHex());
        QJsonObject catalog{{QStringLiteral("Releases"), QJsonArray{}}, {QStringLiteral("LoadingScreens"),
            QJsonObject{{QStringLiteral("PackageBytes"), transport.payload.size()}, {QStringLiteral("PackageSha256"), hash}}}};
        auto parsed = parsePremiumCatalogResponse(catalog); QVERIFY(parsed); QVERIFY(parsed->loadingScreens);
        QCOMPARE(parsed->loadingScreens->bytes, transport.payload.size()); QCOMPARE(parsed->loadingScreens->sha256, hash);
        QVERIFY(!parsePremiumCatalogResponse({}));
        auto legacy = parsePremiumCatalogResponse({{QStringLiteral("Releases"), QJsonArray{}}});
        QVERIFY(legacy); QVERIFY(!legacy->loadingScreens);
        for (const auto bytes : {qint64{0}, qint64{-1}, qint64{64 * 1024 * 1024 + 1}}) {
            catalog[QStringLiteral("LoadingScreens")] = QJsonObject{{QStringLiteral("PackageBytes"), bytes}, {QStringLiteral("PackageSha256"), hash}};
            auto invalid = parsePremiumCatalogResponse(catalog); QVERIFY(invalid); QVERIFY(!invalid->loadingScreens);
        }
        catalog[QStringLiteral("LoadingScreens")] = QJsonObject{{QStringLiteral("PackageBytes"), 10}, {QStringLiteral("PackageSha256"), QStringLiteral("invalid")}};
        parsed = parsePremiumCatalogResponse(catalog); QVERIFY(parsed); QVERIFY(!parsed->loadingScreens);
        const QUrl url(QStringLiteral("https://bluehalo.site/api/installer/premium/addons/loading-screens"));
        QVERIFY(premiumAssetUrl(url));
        for (const auto& bad : {QStringLiteral("http://bluehalo.site/api/installer/premium/addons/loading-screens"),
             QStringLiteral("https://evil.invalid/api/installer/premium/addons/loading-screens"),
             url.toString() + QStringLiteral("?token=x"), url.toString() + QStringLiteral("#fragment"),
             url.toString() + QStringLiteral("/other")}) QVERIFY(!premiumAssetUrl(QUrl(bad)));
        const auto path = temp.filePath(QStringLiteral("addon.rudp"));
        QVERIFY(!downloadAsset(url, transport.payload.size(), hash, path, {}, {}, &transport));
        QCOMPARE(transport.requests, 0);
        QVERIFY(downloadAsset(url, transport.payload.size(), hash, path, {}, {}, &transport, "test-code"));
        QCOMPARE(read(path), transport.payload); QCOMPARE(transport.authorization, QByteArray("Bearer test-code"));
        QVERIFY(QFile::remove(path));
        transport.status = 302; const auto previous = transport.requests;
        QVERIFY(!downloadAsset(url, transport.payload.size(), hash, path, {}, {}, &transport, "test-code"));
        QCOMPARE(transport.requests, previous + 1); // Never forward a Premium bearer through redirects.
        transport.status = 403;
        auto denied = downloadAsset(url, transport.payload.size(), hash, path, {}, {}, &transport, "test-code");
        QVERIFY(!denied); QCOMPARE(denied.error().code, ErrorCode::AccessDenied);
        QVERIFY(!QFileInfo::exists(path));
    }
    void addonRecoveryAndRejection() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); qputenv("XDG_DATA_HOME", temp.path().toUtf8());
        const auto game = temp.filePath(QStringLiteral("game"));
        const auto target = QDir(game).filePath(QStringLiteral("BlueArchive_Data/StreamingAssets/PUB/Resource/Preload/Windows/"
            "prologgroup-assets-_mx-addressableasset-uis-01_common-12_loading-_mxprolog-2024-11-18_assets_all_2654595875.bundle"));
        const QString id(32, 'b'); const auto backup = target + QStringLiteral(".baretool-rollback-") + id + QStringLiteral(".tmp");
        const auto stage = target + QStringLiteral(".baretool-install-") + id + QStringLiteral(".tmp");
        QVERIFY(write(target, "patched fixture")); QVERIFY(write(backup, "original fixture"));
        const auto before = hashFile(backup), after = hashFile(target); QVERIFY(before); QVERIFY(after);
        QJsonObject item{{QStringLiteral("Target"), target}, {QStringLiteral("Backup"), backup}, {QStringLiteral("Stage"), stage},
            {QStringLiteral("Before"), *before}, {QStringLiteral("After"), *after}};
        QJsonObject journal{{QStringLiteral("FormatVersion"), 1}, {QStringLiteral("Id"), id}, {QStringLiteral("Files"), QJsonArray{item}},
            {QStringLiteral("OriginalRetained"), false}, {QStringLiteral("Committed"), false}};
        const auto saveJournal = [&] {
            auto encrypted = platform::protectUser(QJsonDocument(journal).toJson(QJsonDocument::Compact), target.toUtf8());
            return encrypted && write(target + QStringLiteral(".baretool-transaction.dpapi"), *encrypted);
        };
        const auto guard = []() -> Result<void> { return {}; };
        // A journal may only name this addon's target, never arbitrary files.
        item[QStringLiteral("Target")] = temp.filePath(QStringLiteral("unrelated"));
        journal[QStringLiteral("Files")] = QJsonArray{item}; QVERIFY(saveJournal());
        QVERIFY(!installLoadingScreensAddon(game, temp.filePath(QStringLiteral("missing.rudp")), temp.path(), guard));
        QCOMPARE(read(target), QByteArray("patched fixture")); QVERIFY(QFileInfo::exists(backup));
        item[QStringLiteral("Target")] = target; journal[QStringLiteral("Files")] = QJsonArray{item}; QVERIFY(saveJournal());
        // Recovery restores the synthetic file; it is then rejected as an unsupported game bundle.
        QVERIFY(!installLoadingScreensAddon(game, temp.filePath(QStringLiteral("missing.rudp")), temp.path(), guard));
        QCOMPARE(read(target), QByteArray("original fixture")); QVERIFY(!QFileInfo::exists(backup));
        QVERIFY(!QFileInfo::exists(target + QStringLiteral(".baretool-transaction.dpapi")));
        const auto realTarget = temp.filePath(QStringLiteral("actual.bundle")); QVERIFY(QFile::rename(target, realTarget));
        QVERIFY(QFile::link(realTarget, target));
        QVERIFY(!installLoadingScreensAddon(game, {}, temp.path(), guard));
        QCOMPARE(read(realTarget), QByteArray("original fixture"));
    }
    void recoveryJournal() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); qputenv("XDG_DATA_HOME", temp.path().toUtf8());
        const auto game = QDir(temp.path()).filePath(QStringLiteral("game")), target = gameDatabasePath(game);
        const QString id(32, 'a'); const auto backup = target + QStringLiteral(".baretool-rollback-") + id + QStringLiteral(".tmp");
        const auto stage = target + QStringLiteral(".baretool-install-") + id + QStringLiteral(".tmp");
        QVERIFY(write(target, read(fixture("patched.db.fixture")))); QVERIFY(write(backup, read(fixture("original.db.fixture"))));
        const auto before = hashFile(backup), after = hashFile(target); QVERIFY(before); QVERIFY(after);
        const QJsonObject item{{QStringLiteral("Target"), target}, {QStringLiteral("Backup"), backup}, {QStringLiteral("Stage"), stage},
            {QStringLiteral("Before"), *before}, {QStringLiteral("After"), *after}};
        const QJsonObject journal{{QStringLiteral("FormatVersion"), 1}, {QStringLiteral("Id"), id}, {QStringLiteral("Files"), QJsonArray{item}},
            {QStringLiteral("OriginalRetained"), false}, {QStringLiteral("Committed"), false}};
        auto encrypted = platform::protectUser(QJsonDocument(journal).toJson(QJsonDocument::Compact), target.toUtf8()); QVERIFY(encrypted);
        const auto path = target + QStringLiteral(".baretool-transaction.dpapi");
        QVERIFY(write(path, *encrypted)); auto corrupted = *encrypted; corrupted[34] ^= 1; QVERIFY(write(path, corrupted));
        QVERIFY(!recoverInstallation(game)); QCOMPARE(read(target), read(fixture("patched.db.fixture"))); QVERIFY(QFileInfo::exists(backup));
        QVERIFY(write(path, *encrypted));
        const auto key = temp.filePath(QStringLiteral("RuToolInstaller/private/local.key"));
        QVERIFY(QFile::rename(key, key + QStringLiteral(".saved")));
        QVERIFY(!recoverInstallation(game));
        QVERIFY(!QFileInfo::exists(key)); QCOMPARE(read(path), *encrypted);
        QCOMPARE(read(target), read(fixture("patched.db.fixture"))); QVERIFY(QFileInfo::exists(backup));
        QVERIFY(QFile::rename(key + QStringLiteral(".saved"), key));
        QVERIFY(recoverInstallation(game)); QCOMPARE(read(target), read(fixture("original.db.fixture")));
        QVERIFY(!QFileInfo::exists(path)); QVERIFY(!QFileInfo::exists(backup));
    }
    void bundleRecoveryJournal() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); qputenv("XDG_DATA_HOME", temp.path().toUtf8());
        const auto game = temp.filePath(QStringLiteral("game")), database = gameDatabasePath(game);
        const std::vector<QString> paths{database, QFileInfo(database).dir().filePath(QStringLiteral("Excel.zip")),
            QDir(game).filePath(QStringLiteral("BlueArchive_Data/resources.assets"))};
        const QString id(32, 'c'); QJsonArray records;
        for (const auto& target : paths) {
            const auto backup = target + QStringLiteral(".baretool-rollback-") + id + QStringLiteral(".tmp");
            const auto stage = target + QStringLiteral(".baretool-install-") + id + QStringLiteral(".tmp");
            QVERIFY(write(target, "modified bundle fixture")); QVERIFY(write(backup, "original bundle fixture"));
            auto before = hashFile(backup), after = hashFile(target); QVERIFY(before); QVERIFY(after);
            records.append(QJsonObject{{QStringLiteral("Target"), target}, {QStringLiteral("Backup"), backup}, {QStringLiteral("Stage"), stage},
                {QStringLiteral("Before"), *before}, {QStringLiteral("After"), *after}});
        }
        QJsonObject journal{{QStringLiteral("FormatVersion"), 1}, {QStringLiteral("Id"), id}, {QStringLiteral("Files"), records},
            {QStringLiteral("OriginalRetained"), false}, {QStringLiteral("Committed"), false}};
        const auto saveJournal = [&] {
            auto encrypted = platform::protectUser(QJsonDocument(journal).toJson(QJsonDocument::Compact), database.toUtf8());
            return encrypted && write(database + QStringLiteral(".baretool-transaction.dpapi"), *encrypted);
        };
        auto truncated = records; truncated.removeLast(); journal[QStringLiteral("Files")] = truncated; QVERIFY(saveJournal());
        QVERIFY(!recoverInstallation(game));
        for (const auto& path : paths) QCOMPARE(read(path), QByteArray("modified bundle fixture"));
        journal[QStringLiteral("Files")] = records; QVERIFY(saveJournal());
        QVERIFY(recoverInstallation(game));
        for (const auto& path : paths) {
            QCOMPARE(read(path), QByteArray("original bundle fixture"));
            QVERIFY(!QFileInfo::exists(path + QStringLiteral(".baretool-rollback-") + id + QStringLiteral(".tmp")));
        }
        QVERIFY(!QFileInfo::exists(database + QStringLiteral(".baretool-transaction.dpapi")));
    }
    void networkPolicy() {
        QVERIFY(ServiceClient::allowedEndpoint(QUrl(QStringLiteral("https://bluehalo.site/api/installer/application/v2/latest"))));
        QVERIFY(!ServiceClient::allowedEndpoint(QUrl(QStringLiteral("https://bluehalo.site/api/installer/application/latest"))));
        QVERIFY(allowedAssetUrl(QUrl(QStringLiteral("https://bluehalo.site/api/installer/assets/test"))));
        QVERIFY(!allowedAssetUrl(QUrl(QStringLiteral("http://bluehalo.site/api/installer/assets/test"))));
        QVERIFY(!allowedAssetUrl(QUrl(QStringLiteral("https://evil.invalid/api/installer/assets/test"))));
        QVERIFY(!ServiceClient::allowedEndpoint(QUrl(QStringLiteral("http://bluehalo.site/healthz"))));
        QVERIFY(!ServiceClient::allowedEndpoint(QUrl(QStringLiteral("https://bluehalo.site/healthz?token=x"))));
    }
};
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() == 3 && arguments[1] == QStringLiteral("--lock-process-fixture")) {
        auto lock = platform::FileLock::operation(arguments[2]);
        if (!lock || ::write(STDOUT_FILENO, "ready", 5) != 5) return 1;
        QTimer::singleShot(20000, &app, &QCoreApplication::quit); return app.exec();
    }
    if (arguments.size() == 4 && arguments[1] == QStringLiteral("--install-crash-fixture")) {
        QTemporaryDir prepared(QDir(arguments[2]).filePath(QStringLiteral("prepared-XXXXXX")));
        if (!prepared.isValid()) return 1;
        platform::Secret password(QByteArray("public-test-password"));
        auto package = decryptPackage(fixture("sample.rut.fixture"), prepared.path(), password);
        if (!package) return 1;
        InstallationRequest request; request.game = arguments[2]; request.package = *package;
        request.backupRoot = arguments[3]; request.originalVerified = true;
        const auto target = QFile::encodeName(gameDatabasePath(request.game));
        syncFailureTarget = target.constData(); crashAfterRename = true;
        auto result = installPackage(request, []() -> Result<void> { return {}; });
        return result ? 0 : 2; // The injected abrupt exit must terminate with 77.
    }
    if (app.arguments().contains(QStringLiteral("--game-process-fixture"))) {
        if (::prctl(PR_SET_NAME, "BlueArchive.exe", 0, 0, 0) != 0) return 1;
        if (::write(STDOUT_FILENO, "ready", 5) != 5) return 1;
        QTimer::singleShot(20000, &app, &QCoreApplication::quit); return app.exec();
    }
    LinuxTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "LinuxTests.moc"
