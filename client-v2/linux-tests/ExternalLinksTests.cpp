#include <QtTest>
#include <QTemporaryDir>
#include <QScopeGuard>
#include "ExternalLinks.hpp"

class ExternalLinksTests final : public QObject {
    Q_OBJECT
private slots:
    void sessionEnvironment() {
        QProcessEnvironment env;
        env.insert(QStringLiteral("LD_LIBRARY_PATH"), QStringLiteral("/custom/lib"));
        QCOMPARE(ExternalLinks::desktopEnvironment(env), env); // Unpackaged launch.
        env.insert(QStringLiteral("RUTOOL_DESKTOP_ENV_SAVED"), QStringLiteral("1"));
        env.insert(QStringLiteral("RUTOOL_DESKTOP_LD_LIBRARY_PATH"), QStringLiteral("/session/lib"));
        env.insert(QStringLiteral("RUTOOL_DESKTOP_LD_PRELOAD"), QStringLiteral("libm.so.6"));
        env.insert(QStringLiteral("LD_PRELOAD"), QStringLiteral("/bundle/libstdc++.so.6"));
        env.insert(QStringLiteral("RUTOOL_ORIGINAL_LD_PRELOAD"), QStringLiteral("libm.so.6"));
        env.insert(QStringLiteral("QT_PLUGIN_PATH"), QStringLiteral("/bundle/plugins"));
        env.insert(QStringLiteral("QML_IMPORT_PATH"), QStringLiteral("/bundle/qml"));
        env.insert(QStringLiteral("APPDIR"), QStringLiteral("/bundle"));
        env.insert(QStringLiteral("WAYLAND_DISPLAY"), QStringLiteral("wayland-0"));
        env.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"), QStringLiteral("unix:path=/run/user/1000/bus"));
        env.insert(QStringLiteral("XDG_ACTIVATION_TOKEN"), QStringLiteral("session-token"));
        const auto clean = ExternalLinks::desktopEnvironment(env);
        QCOMPARE(clean.value(QStringLiteral("LD_LIBRARY_PATH")), QStringLiteral("/session/lib"));
        QCOMPARE(clean.value(QStringLiteral("LD_PRELOAD")), QStringLiteral("libm.so.6"));
        for (const auto* key : {"QT_PLUGIN_PATH", "QML_IMPORT_PATH", "APPDIR", "RUTOOL_ORIGINAL_LD_PRELOAD", "RUTOOL_DESKTOP_ENV_SAVED"})
            QVERIFY(!clean.contains(QString::fromLatin1(key)));
        for (const auto* key : {"WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS", "XDG_ACTIVATION_TOKEN"})
            QCOMPARE(clean.value(QString::fromLatin1(key)), env.value(QString::fromLatin1(key)));
        // Cleaning a child environment must not change the input.
        QCOMPARE(env.value(QStringLiteral("QT_PLUGIN_PATH")), QStringLiteral("/bundle/plugins"));
    }
    void launchAndErrors() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto output = directory.filePath(QStringLiteral("opened.txt"));
        QFile opener(directory.filePath(QStringLiteral("xdg-open")));
        QVERIFY(opener.open(QIODevice::WriteOnly));
        opener.write("#!/bin/sh\nprintf '%s\\n' \"$#\" \"$1\" \"${LD_LIBRARY_PATH-unset}\" \"${QT_PLUGIN_PATH-unset}\" \"$DBUS_SESSION_BUS_ADDRESS\" > \"$RUTOOL_TEST_OUTPUT\"\n");
        opener.close(); QVERIFY(opener.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const auto before = QProcessEnvironment::systemEnvironment();
        const auto restore = qScopeGuard([&] {
            for (const auto& key : QProcessEnvironment::systemEnvironment().keys())
                if (!before.contains(key)) qunsetenv(key.toLocal8Bit());
            for (const auto& key : before.keys()) qputenv(key.toLocal8Bit(), before.value(key).toLocal8Bit());
        });
        qputenv("PATH", directory.path().toUtf8());
        qputenv("RUTOOL_TEST_OUTPUT", output.toUtf8());
        qputenv("RUTOOL_DESKTOP_ENV_SAVED", "1");
        qputenv("RUTOOL_DESKTOP_LD_LIBRARY_PATH", "/session/lib");
        qunsetenv("RUTOOL_DESKTOP_QT_PLUGIN_PATH");
        qputenv("QT_PLUGIN_PATH", "/bundle/plugins");
        qputenv("DBUS_SESSION_BUS_ADDRESS", "session-bus");
        ExternalLinks links;
        QSignalSpy failed(&links, &ExternalLinks::failed);
        const QUrl url(QStringLiteral("https://example.invalid/a?text=hello%20world&shell=$(false)"));
        links.open(url);
        QVERIFY(QTest::qWaitFor([&] { return QFileInfo(output).size() > 0; }, 3000));
        QFile captured(output); QVERIFY(captured.open(QIODevice::ReadOnly));
        const auto lines = captured.readAll().split('\n');
        QCOMPARE(lines.value(0), QByteArray("1"));
        QCOMPARE(lines.value(1), url.toEncoded());
        QCOMPARE(lines.value(2), QByteArray("/session/lib"));
        QCOMPARE(lines.value(3), QByteArray("unset"));
        QCOMPARE(lines.value(4), QByteArray("session-bus"));
        QCOMPARE(failed.count(), 0);
        QCOMPARE(qgetenv("QT_PLUGIN_PATH"), QByteArray("/bundle/plugins"));
        links.open(QUrl(QStringLiteral("file:///etc/passwd")));
        QCOMPARE(failed.count(), 1);
        QVERIFY(opener.remove());
        links.open(url);
        QCOMPARE(failed.count(), 2);
    }
};
QTEST_GUILESS_MAIN(ExternalLinksTests)
#include "ExternalLinksTests.moc"
