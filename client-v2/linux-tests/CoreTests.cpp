#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcess>
#include <QCryptographicHash>
#include <QtEndian>
#include <QNetworkReply>
#include <QTimer>
#include <rutool/platform/FileOps.hpp>
#include <rutool/platform/Crypto.hpp>
#include <rutool/security/Signatures.hpp>
#include <rutool/core/Installation.hpp>
#include <rutool/network/Transfers.hpp>
#include <rutool/network/ServiceClient.hpp>
#include <rutool/network/ClientContent.hpp>
#include <rutool/network/Premium.hpp>
#include "PortableCrypto.hpp"
#include "TestFiles.hpp"
#include "TestNetwork.hpp"
#include <cstring>

using namespace rutool;
using namespace rutool::test;
namespace {
crypto::Bytes view(const QByteArray& bytes) {
    return {reinterpret_cast<const unsigned char*>(bytes.constData()), static_cast<size_t>(bytes.size())};
}
}
// Shared behavior: no Linux syscall overrides or process fixtures in this executable.
class ReportCaptureTransport final : public QNetworkAccessManager {
public:
    QByteArray body;
    int requests{};
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice* input) override {
        ++requests;
        body = input ? input->readAll() : QByteArray{};
        const auto response = QJsonDocument(QJsonObject{{QStringLiteral("nextAllowedAtUtc"),
            QDateTime::currentDateTimeUtc().addSecs(60).toString(Qt::ISODateWithMs)}}).toJson();
        return new FixtureReply(request, response, 200, false, this);
    }
};
class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void creditsSurviveServerContent() {
        const auto countLocal = [](const QVariantList& entries) {
            return std::count_if(entries.begin(), entries.end(), [](const QVariant& value) {
                const auto entry = value.toMap();
                return entry.value(QStringLiteral("role")) == QStringLiteral("Разработчик Linux-версии") &&
                    entry.value(QStringLiteral("name")) == QStringLiteral("tskitsune");
            });
        };
        const auto defaults = defaultCredits();
#ifdef Q_OS_LINUX
        QCOMPARE(countLocal(defaults), 1);
        QCOMPARE(defaults[0].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Shizuka"));
        QCOMPARE(defaults[1].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("tskitsune"));
#else
        QCOMPARE(countLocal(defaults), 0);
#endif
        const auto license = QStringLiteral("Test license");
        QJsonObject object{{QStringLiteral("formatVersion"), 1}, {QStringLiteral("revision"), 1},
            {QStringLiteral("licenseText"), license}, {QStringLiteral("licenseSha256"),
                QString::fromLatin1(QCryptographicHash::hash(license.toUtf8(), QCryptographicHash::Sha256).toHex())}};
        auto parsed = parseClientContent(QJsonDocument(object).toJson()); QVERIFY(parsed);
        QCOMPARE(countLocal(parsed->credits), countLocal(defaults));
        object[QStringLiteral("credits")] = QJsonArray{
            QJsonObject{{QStringLiteral("role"), QStringLiteral("Совладелец")}, {QStringLiteral("name"), QStringLiteral("Ui")}},
            QJsonObject{{QStringLiteral("role"), QStringLiteral("Кодер, автор, редактор")}, {QStringLiteral("name"), QStringLiteral("Shizuka")}},
            QJsonObject{{QStringLiteral("role"), QStringLiteral("Разработчик Linux-версии")}, {QStringLiteral("name"), QStringLiteral("tskitsune")}}};
        parsed = parseClientContent(QJsonDocument(object).toJson()); QVERIFY(parsed);
        QCOMPARE(countLocal(parsed->credits), 1);
        QCOMPARE(parsed->credits[0].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Ui"));
        QCOMPARE(parsed->credits[1].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Shizuka"));
        QCOMPARE(parsed->credits[2].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("tskitsune"));
        object[QStringLiteral("credits")] = QJsonArray{};
        parsed = parseClientContent(QJsonDocument(object).toJson()); QVERIFY(parsed);
        QCOMPARE(countLocal(parsed->credits), countLocal(defaults));
    }
    void reportPlatformInMultipart() {
        ReportCaptureTransport transport;
        const auto fox = QString::fromUtf8(QByteArray::fromHex("f09fa68a"));
        ReportRequest request{QString(32, 'a'), QStringLiteral("contact"), fox.repeated(reportTextLimit()), QStringLiteral("Техническая проблема"), {}};
        auto sent = sendReport(request, {}, &transport); QVERIFY(sent); QVERIFY(sent->accepted);
        const auto prefix = reportPlatformPrefix();
#ifdef Q_OS_LINUX
        QCOMPARE(prefix, QStringLiteral("[Linux]\n")); QCOMPARE(reportTextLimit(), 392);
#else
        QVERIFY(prefix.isEmpty()); QCOMPARE(reportTextLimit(), 400);
#endif
        QCOMPARE((prefix + request.text).toUcs4().size(), 400);
        QVERIFY(transport.body.contains(QByteArray("name=\"text\"\r\n\r\n") + (prefix + request.text).toUtf8() + "\r\n"));
        QVERIFY(transport.body.contains(QByteArray("name=\"topic\"\r\n\r\n") + request.topic.toUtf8() + "\r\n"));
        request.text += fox;
        QVERIFY(!sendReport(request, {}, &transport)); QCOMPARE(transport.requests, 1);
        request.text.clear();
        QVERIFY(!sendReport(request, {}, &transport)); QCOMPARE(transport.requests, 1);
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
        QVERIFY(write(dir.filePath(QStringLiteral("bad.rut")), read(fixture("sample.rut.fixture")).left(60)));
        const auto badFolder = dir.filePath(QStringLiteral("bad")); QVERIFY(QDir().mkpath(badFolder));
        QVERIFY(!decryptPackage(dir.filePath(QStringLiteral("bad.rut")), badFolder, password));
        auto corrupt = read(fixture("sample.rut.fixture")); corrupt[50] ^= 1; QVERIFY(write(dir.filePath(QStringLiteral("bad.rut")), corrupt));
        QVERIFY(!decryptPackage(dir.filePath(QStringLiteral("bad.rut")), badFolder, password));
        platform::Secret wrong(QByteArray("wrong password")); QVERIFY(!decryptPackage(fixture("sample.rut.fixture"), badFolder, wrong));
        auto cancelled = std::make_shared<std::atomic_bool>(true); QVERIFY(!decryptPackage(fixture("sample.rut.fixture"), badFolder, password, cancelled));
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
    QTemporaryDir data;
    if (!data.isValid()) return 1;
    qputenv("XDG_DATA_HOME", data.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", data.path().toUtf8());
    qputenv("XDG_CACHE_HOME", data.path().toUtf8());
    QCoreApplication app(argc, argv);
    CoreTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "CoreTests.moc"
