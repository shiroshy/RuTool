#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QCryptographicHash>
#include <rutool/platform/Runtime.hpp>
#include <rutool/platform/FileOps.hpp>
#include <rutool/platform/Crypto.hpp>
#include <rutool/security/Signatures.hpp>
#include <rutool/core/Installation.hpp>
#include <rutool/core/InstallerConfig.hpp>
#include <rutool/network/Transfers.hpp>
#include <rutool/network/ServiceClient.hpp>
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
namespace { thread_local const char* syncFailureTarget{}; }
extern "C" int fsync(int fd) {
    if (syncFailureTarget) {
        struct stat info{};
        if (::fstat(fd, &info) == 0 && S_ISDIR(info.st_mode)) {
            const int targetFd = ::open(syncFailureTarget, O_RDONLY | O_CLOEXEC);
            char prefix[8]{};
            const auto count = targetFd >= 0 ? ::read(targetFd, prefix, sizeof(prefix)) : -1;
            if (targetFd >= 0) ::close(targetFd);
            if (count == 8 && std::memcmp(prefix, "translat", 8) == 0) {
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
    int status{200}; bool badRange{};
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
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
        const auto overridePath = QDir(temp.path()).filePath(QStringLiteral("rutool/") + name);
        QVERIFY(write(overridePath, "{\"FormatVersion\":1,\"RutPassword\":\"test-only-password\"}"));
        QCOMPARE(installerConfigPath(name), overridePath);
        auto password = readPackagePassword(installerConfigPath(name));
        QVERIFY(password); QCOMPARE(password->bytes(), QByteArray("test-only-password"));
        QVERIFY(write(overridePath, "{}"));
        QCOMPARE(installerConfigPath(name), overridePath);
        QVERIFY(!readPackagePassword(installerConfigPath(name)));
        QVERIFY(QFile::remove(overridePath));
        QCOMPARE(installerConfigPath(name), bundled);
        QVERIFY(QFile::link(temp.filePath(QStringLiteral("missing")), overridePath));
        QCOMPARE(installerConfigPath(name), overridePath);
        QVERIFY(!readPackagePassword(installerConfigPath(name)));
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
        auto package = decryptPackage(fixture("sample.rut"), prepared, password); QVERIFY2(package, package ? "" : qPrintable(package.error().message));
        const auto s = package->signature;
        RutSignature signature{1, s.value(QStringLiteral("FileSha256")).toString(), s.value(QStringLiteral("RuToolVersion")).toString(),
            s.value(QStringLiteral("WatermarkSha256")).toString(), s.value(QStringLiteral("Algorithm")).toString(),
            s.value(QStringLiteral("SignerId")).toString(), s.value(QStringLiteral("SignatureBase64")).toString()};
        QVERIFY(verifyRutSignature(signature, read(fixture("public.der"))));
        const auto game = dir.filePath(QStringLiteral("game")), database = gameDatabasePath(game);
        QVERIFY(write(database, read(fixture("original.db"))));
        InstallationRequest request; request.game = game; request.package = *package;
        request.backupRoot = dir.filePath(QStringLiteral("backups")); request.originalVerified = true;
        const auto deny = []() -> Result<void> { return std::unexpected(Error{ErrorCode::Signature, QStringLiteral("test guard rejected")}); };
        QVERIFY(!installPackage(request, deny)); QCOMPARE(read(database), read(fixture("original.db")));
        const auto guard = [&]() -> Result<void> {
            if (!verifyRutSignature(signature, read(fixture("public.der")))) return std::unexpected(Error{ErrorCode::Signature, QStringLiteral("test signature rejected")});
            return {};
        };
        auto installed = installPackage(request, guard); QVERIFY2(installed, installed ? "" : qPrintable(installed.error().message));
        QCOMPARE(read(database), read(fixture("patched.db"))); QVERIFY(!installed->originalBackup.isEmpty());
        auto receipt = originalReceipt(game, installed->originalBackup); QVERIFY(receipt);
        auto duplicate = installPackage(request, guard); QVERIFY(duplicate); QVERIFY(duplicate->alreadyInstalled);
        auto restored = restoreOriginal(game, installed->originalBackup); QVERIFY2(restored, restored ? "" : qPrintable(restored.error().message));
        QCOMPARE(read(database), read(fixture("original.db")));
        const auto encodedTarget = QFile::encodeName(database);
        syncFailureTarget = encodedTarget.constData();
        auto durabilityFailure = installPackage(request, guard);
        const bool injected = syncFailureTarget == nullptr; syncFailureTarget = nullptr;
        QVERIFY(injected); QVERIFY(!durabilityFailure);
        QCOMPARE(read(database), read(fixture("original.db")));
        QVERIFY(!QFileInfo::exists(database + QStringLiteral(".baretool-transaction.dpapi")));
        QVERIFY(write(dir.filePath(QStringLiteral("bad.rut")), read(fixture("sample.rut")).left(60)));
        const auto badFolder = dir.filePath(QStringLiteral("bad")); QVERIFY(QDir().mkpath(badFolder));
        QVERIFY(!decryptPackage(dir.filePath(QStringLiteral("bad.rut")), badFolder, password));
        auto corrupt = read(fixture("sample.rut")); corrupt[50] ^= 1; QVERIFY(write(dir.filePath(QStringLiteral("bad.rut")), corrupt));
        QVERIFY(!decryptPackage(dir.filePath(QStringLiteral("bad.rut")), badFolder, password));
        platform::Secret wrong(QByteArray("wrong password")); QVERIFY(!decryptPackage(fixture("sample.rut"), badFolder, wrong));
        auto cancelled = std::make_shared<std::atomic_bool>(true); QVERIFY(!decryptPackage(fixture("sample.rut"), badFolder, password, cancelled));
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
    void recoveryJournal() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); qputenv("XDG_DATA_HOME", temp.path().toUtf8());
        const auto game = QDir(temp.path()).filePath(QStringLiteral("game")), target = gameDatabasePath(game);
        const QString id(32, 'a'); const auto backup = target + QStringLiteral(".baretool-rollback-") + id + QStringLiteral(".tmp");
        const auto stage = target + QStringLiteral(".baretool-install-") + id + QStringLiteral(".tmp");
        QVERIFY(write(target, read(fixture("patched.db")))); QVERIFY(write(backup, read(fixture("original.db"))));
        const auto before = hashFile(backup), after = hashFile(target); QVERIFY(before); QVERIFY(after);
        const QJsonObject item{{QStringLiteral("Target"), target}, {QStringLiteral("Backup"), backup}, {QStringLiteral("Stage"), stage},
            {QStringLiteral("Before"), *before}, {QStringLiteral("After"), *after}};
        const QJsonObject journal{{QStringLiteral("FormatVersion"), 1}, {QStringLiteral("Id"), id}, {QStringLiteral("Files"), QJsonArray{item}},
            {QStringLiteral("OriginalRetained"), false}, {QStringLiteral("Committed"), false}};
        auto encrypted = platform::protectUser(QJsonDocument(journal).toJson(QJsonDocument::Compact), target.toUtf8()); QVERIFY(encrypted);
        const auto path = target + QStringLiteral(".baretool-transaction.dpapi");
        QVERIFY(write(path, *encrypted)); auto corrupted = *encrypted; corrupted[34] ^= 1; QVERIFY(write(path, corrupted));
        QVERIFY(!recoverInstallation(game)); QCOMPARE(read(target), read(fixture("patched.db"))); QVERIFY(QFileInfo::exists(backup));
        QVERIFY(write(path, *encrypted)); QVERIFY(recoverInstallation(game)); QCOMPARE(read(target), read(fixture("original.db")));
        QVERIFY(!QFileInfo::exists(path)); QVERIFY(!QFileInfo::exists(backup));
    }
    void networkPolicy() {
        QVERIFY(allowedAssetUrl(QUrl(QStringLiteral("https://bluehalo.site/api/installer/assets/test"))));
        QVERIFY(!allowedAssetUrl(QUrl(QStringLiteral("http://bluehalo.site/api/installer/assets/test"))));
        QVERIFY(!allowedAssetUrl(QUrl(QStringLiteral("https://evil.invalid/api/installer/assets/test"))));
        QVERIFY(!ServiceClient::allowedEndpoint(QUrl(QStringLiteral("http://bluehalo.site/healthz"))));
        QVERIFY(!ServiceClient::allowedEndpoint(QUrl(QStringLiteral("https://bluehalo.site/healthz?token=x"))));
    }
};
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--game-process-fixture"))) {
        if (::prctl(PR_SET_NAME, "BlueArchive.exe", 0, 0, 0) != 0) return 1;
        if (::write(STDOUT_FILENO, "ready", 5) != 5) return 1;
        QTimer::singleShot(20000, &app, &QCoreApplication::quit); return app.exec();
    }
    LinuxTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "LinuxTests.moc"
