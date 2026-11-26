#include <QtTest>
#include <QtConcurrent>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QProcess>
#include <rutool/network/LinuxUpdates.hpp>
#include <rutool/platform/AppImageUpdates.hpp>
#include <rutool/platform/FileOps.hpp>
#include "TestFiles.hpp"
#include "TestNetwork.hpp"
#include "TestSigning.hpp"
#include "TestImages.hpp"

using namespace rutool;
using namespace rutool::test;
namespace {
QJsonObject route(const QString& from, const QString& next, const QByteArray& payload) {
    return {{QStringLiteral("FromVersion"), from}, {QStringLiteral("BeforeVersion"), next},
        {QStringLiteral("Version"), next}, {QStringLiteral("Tag"), u'v' + next},
        {QStringLiteral("Asset"), QStringLiteral("RuTool-x86_64.AppImage")}, {QStringLiteral("Bytes"), payload.size()},
        {QStringLiteral("Sha256"), QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex())}};
}
QJsonObject channel(const QJsonArray& routes) {
    return {{QStringLiteral("FormatVersion"), 1}, {QStringLiteral("Platform"), QStringLiteral("linux-x86_64")},
        {QStringLiteral("Repository"), QStringLiteral("example/RuTool")}, {QStringLiteral("Sequence"), 7},
        {QStringLiteral("Routes"), routes}};
}
}
class LinuxUpdateTests final : public QObject {
    Q_OBJECT
private slots:
    void signedRoutesAndKeyTransition() {
        const auto first = route(QStringLiteral("0.0.0"), QStringLiteral("2.2.0"), image("bridge"));
        const auto second = route(QStringLiteral("2.2.0"), QStringLiteral("3.0.0"), image("latest"));
        const auto old = signFixture(channel({first}));
        const auto next = signFixture(channel({second}), 2);
        QVERIFY(!old.envelope.isEmpty()); QVERIFY(!next.envelope.isEmpty());
        auto bridge = verifyLinuxChannel(old.envelope, old.publicKey, QStringLiteral("example/RuTool"), QStringLiteral("2.1.1"), 0);
        QVERIFY(bridge); QVERIFY(bridge->next); QCOMPARE(bridge->next->version, QStringLiteral("2.2.0"));
        QVERIFY(!verifyLinuxChannel(next.envelope, old.publicKey, QStringLiteral("example/RuTool"), QStringLiteral("2.2.0"), 0));
        auto latest = verifyLinuxChannel(next.envelope, next.publicKey, QStringLiteral("example/RuTool"), QStringLiteral("2.2.0"), 0);
        QVERIFY(latest); QVERIFY(latest->next); QCOMPARE(latest->next->version, QStringLiteral("3.0.0"));
        auto done = verifyLinuxChannel(next.envelope, next.publicKey, QStringLiteral("example/RuTool"), QStringLiteral("3.0.0"), 7);
        QVERIFY(done); QVERIFY(!done->next);
        const auto combined = signFixture(channel({first, second}));
        QVERIFY(verifyLinuxChannel(combined.envelope, combined.publicKey, QStringLiteral("example/RuTool"), QStringLiteral("2.1.1"), 7));
    }
    void rejectsInvalidChannels() {
        const auto good = route(QStringLiteral("0.0.0"), QStringLiteral("2.2.0"), image("new"));
        const auto payload = channel({good}); const auto signedGood = signFixture(payload); QVERIFY(!signedGood.envelope.isEmpty());
        QVERIFY(!verifyLinuxChannel(signedGood.envelope, signedGood.publicKey, QStringLiteral("other/repo"), QStringLiteral("2.1.1"), 0));
        QVERIFY(!verifyLinuxChannel(signedGood.envelope, signedGood.publicKey, QStringLiteral("example/RuTool"), QStringLiteral("2.1.1"), 8));
        auto corrupted = signedGood.envelope; corrupted[30] ^= 1;
        QVERIFY(!verifyLinuxChannel(corrupted, signedGood.publicKey, QStringLiteral("example/RuTool"), QStringLiteral("2.1.1"), 0));
        QList<QJsonObject> invalid;
        auto bad = payload; bad[QStringLiteral("FormatVersion")] = 2; invalid.append(bad);
        bad = payload; bad[QStringLiteral("Platform")] = QStringLiteral("windows-x86_64"); invalid.append(bad);
        bad = payload; bad[QStringLiteral("Routes")] = QJsonArray{good, good}; invalid.append(bad);
        for (const auto& field : {QStringLiteral("Version"), QStringLiteral("Asset"), QStringLiteral("Sha256"), QStringLiteral("FromVersion")}) {
            auto entry = good; entry[field] = QStringLiteral("../bad"); bad = payload; bad[QStringLiteral("Routes")] = QJsonArray{entry}; invalid.append(bad);
        }
        auto entry = good; entry[QStringLiteral("Version")] = QStringLiteral("2.1.0");
        bad = payload; bad[QStringLiteral("Routes")] = QJsonArray{entry}; invalid.append(bad);
        for (const auto& object : invalid) {
            const auto fixture = signFixture(object); QVERIFY(!fixture.envelope.isEmpty());
            QVERIFY(!verifyLinuxChannel(fixture.envelope, fixture.publicKey, QStringLiteral("example/RuTool"), QStringLiteral("2.1.1"), 0));
        }
    }
    void boundedMetadataTransport() {
        for (const int status : {200, 302, 403}) {
            auto future = QtConcurrent::run([status] {
                FixtureTransport transport; transport.status = status; transport.payload = "metadata";
                return fetchLinuxChannel(QStringLiteral("example/RuTool"), QStringLiteral("linux-updates-v1"), {}, &transport);
            });
            QVERIFY(QTest::qWaitFor([&] { return future.isFinished(); }, 5000)); const auto result = future.result();
            QCOMPARE(result.has_value(), status == 200);
            if (result) QCOMPARE(*result, QByteArray("metadata"));
        }
        auto future = QtConcurrent::run([] {
            FixtureTransport transport; transport.payload = QByteArray(1024 * 1024 + 1, 'x');
            return fetchLinuxChannel(QStringLiteral("example/RuTool"), QStringLiteral("linux-updates-v1"), {}, &transport);
        });
        QVERIFY(QTest::qWaitFor([&] { return future.isFinished(); }, 5000)); QVERIFY(!future.result());
        auto cancelled = std::make_shared<std::atomic_bool>(true);
        future = QtConcurrent::run([cancelled] { return fetchLinuxChannel(QStringLiteral("example/RuTool"), QStringLiteral("linux-updates-v1"), cancelled); });
        QVERIFY(QTest::qWaitFor([&] { return future.isFinished(); }, 5000)); QVERIFY(!future.result()); QCOMPARE(future.result().error().code, ErrorCode::Cancelled);
    }
    void channelSigningTool() {
#ifdef RUTOOL_TEST_OPENSSL
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto key = temp.filePath(QStringLiteral("public-test-private.der"));
        const auto input = temp.filePath(QStringLiteral("channel.json"));
        const auto output = temp.filePath(QStringLiteral("envelope.json"));
        const auto publicKey = temp.filePath(QStringLiteral("public.der"));
        const auto scalar = QByteArray(31, '\0') + '\1';
        QVERIFY(write(key, QByteArray::fromHex("30310201010420") + scalar + QByteArray::fromHex("a00a06082a8648ce3d030107")));
        QVERIFY(write(input, QJsonDocument(channel({route(QStringLiteral("0.0.0"), QStringLiteral("2.2.0"), image("signed"))})).toJson()));
        QProcess signer;
        const QStringList arguments{QStringLiteral(RUTOOL_CHANNEL_SIGNER), input, output, QStringLiteral("--key"), key,
            QStringLiteral("--public-key"), publicKey, QStringLiteral("--openssl"), QStringLiteral(RUTOOL_TEST_OPENSSL)};
        signer.start(QStringLiteral(RUTOOL_PYTHON), arguments);
        QVERIFY(signer.waitForFinished(10000)); QCOMPARE(signer.exitCode(), 0);
        auto verified = verifyLinuxChannel(read(output), read(publicKey), QStringLiteral("example/RuTool"), QStringLiteral("2.1.1"), 0);
        QVERIFY(verified); QVERIFY(verified->next);
        const auto previous = read(output);
        signer.start(QStringLiteral(RUTOOL_PYTHON), arguments);
        QVERIFY(signer.waitForFinished(10000)); QVERIFY(signer.exitCode() != 0); QCOMPARE(read(output), previous);
#else
        QSKIP("OpenSSL CLI unavailable; signing-tool integration needs OpenSSL");
#endif
    }
    void signedImageReplacement() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto current = temp.filePath(QStringLiteral("RuTool.AppImage"));
        const auto original = image("original"), replacement = image("next");
        QVERIFY(write(current, original)); QVERIFY(QFile::setPermissions(current, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        const auto metadata = signFixture(channel({route(QStringLiteral("0.0.0"), QStringLiteral("2.2.0"), replacement)}));
        auto parsed = verifyLinuxChannel(metadata.envelope, metadata.publicKey, QStringLiteral("example/RuTool"), QStringLiteral("2.1.1"), 0);
        QVERIFY(parsed); QVERIFY(parsed->next);
        FixtureTransport transport; transport.payload = replacement;
        const auto originalHash = *hashFile(current);
        auto installed = platform::installAppImageUpdate(*parsed->next, current, current, *hashFile(current), {}, {}, &transport);
        QVERIFY2(installed, installed ? "" : qPrintable(installed.error().message));
        QCOMPARE(read(current), replacement); QCOMPARE(read(current + QStringLiteral(".rutool-previous.AppImage")), original);
        QVERIFY(QFile::permissions(current).testFlag(QFileDevice::ExeOwner));
        QVERIFY(!platform::installAppImageUpdate(*parsed->next, current, current, originalHash, {}, {}, &transport));
        QCOMPARE(read(current), replacement);
        const auto other = temp.filePath(QStringLiteral("New.AppImage"));
        QVERIFY(platform::installAppImageUpdate(*parsed->next, current, other, *hashFile(current), {}, {}, &transport));
        QCOMPARE(read(other), replacement); QCOMPARE(read(current), replacement);
        QVERIFY(!platform::installAppImageUpdate(*parsed->next, current, other, *hashFile(current), {}, {}, &transport));
        const auto preserved = read(current); transport.payload = image("corrupt");
        auto bad = *parsed->next; bad.sha256 = QString(64, '0');
        QVERIFY(!platform::installAppImageUpdate(bad, current, current, *hashFile(current), {}, {}, &transport));
        QCOMPARE(read(current), preserved);
        auto cancel = std::make_shared<std::atomic_bool>(true);
        QVERIFY(!platform::installAppImageUpdate(*parsed->next, current, current, *hashFile(current), cancel, {}, &transport));
        QCOMPARE(read(current), preserved);
    }
};
QTEST_GUILESS_MAIN(LinuxUpdateTests)
#include "LinuxUpdateTests.moc"
