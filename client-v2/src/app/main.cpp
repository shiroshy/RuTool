#include "ClientController.hpp"
#include "UiState.hpp"
#include <rutool/platform/Runtime.hpp>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTimer>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QCommandLineParser>

#ifdef RUTOOL_NATIVE_PAYLOAD
extern "C" __declspec(dllexport) int rutoolMain(int argc, char* argv[]) {
    const auto payload = QDir(rutool::platform::applicationRoot()).filePath(QStringLiteral("app"));
    for (const auto* name : {"QT_QPA_PLATFORM_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "QT_QUICK_CONTROLS_CONF", "QML_DISK_CACHE_PATH"}) qunsetenv(name);
    const auto plugins = QDir(payload).filePath(QStringLiteral("plugins"));
    qputenv("QT_PLUGIN_PATH", plugins.toUtf8());
    QCoreApplication::setLibraryPaths({plugins});
#else
int main(int argc, char* argv[]) {
#endif
    QGuiApplication app(argc, argv);
#ifdef Q_OS_LINUX
    QCoreApplication::setApplicationName(QStringLiteral("RuToolInstaller"));
#else
    QCoreApplication::setApplicationName(QStringLiteral("RuTool 2 Preview"));
#endif
    QCoreApplication::setOrganizationName(QStringLiteral("RuTool"));
    QCoreApplication::setApplicationVersion(QStringLiteral(RUTOOL_APP_VERSION));
    QCommandLineParser parser;
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({QStringLiteral("smoke-test"), QStringLiteral("Load production QML offline with isolated temporary data, then exit.")});
    parser.addOption({QStringLiteral("smoke-screenshot"), QStringLiteral("Save the smoke-test window as PNG."), QStringLiteral("path")});
    parser.process(app);
    const bool smoke = parser.isSet(QStringLiteral("smoke-test"));
    QTemporaryDir smokeData;
    if (smoke) {
        if (!smokeData.isValid()) return 3;
        qputenv("XDG_DATA_HOME", smokeData.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", smokeData.path().toUtf8());
        qputenv("XDG_CACHE_HOME", smokeData.path().toUtf8());
    }
    app.setProperty("rutoolSmokeTest", smoke);
    app.setWindowIcon(QIcon(QStringLiteral(":/assets/RuTool.ico")));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    ClientController controller;
    UiState uiState(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("settings.ini")), !smoke);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &uiState, &UiState::saveWindow);
    QQmlApplicationEngine engine;
#ifdef RUTOOL_NATIVE_PAYLOAD
    engine.addImportPath(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("app/qml")));
#endif
    bool qmlWarnings{};
    QObject::connect(&engine, &QQmlEngine::warnings, &app, [&controller, &qmlWarnings](const QList<QQmlError>& errors) {
        qmlWarnings = qmlWarnings || !errors.isEmpty();
        for (const auto& error : errors) controller.recordRuntimeError(error.toString());
    });
    engine.rootContext()->setContextProperty(QStringLiteral("client"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("uiState"), &uiState);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(2); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("RuTool.App"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) return 2;
    if (smoke) {
        QTimer::singleShot(1000, &app, [&] {
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            const auto screenshot = parser.value(QStringLiteral("smoke-screenshot"));
            const bool rendered = window && window->isVisible() && (screenshot.isEmpty() || window->grabWindow().save(screenshot));
            QCoreApplication::exit(rendered && !qmlWarnings ? 0 : 4);
        });
    }
    return app.exec();
}
