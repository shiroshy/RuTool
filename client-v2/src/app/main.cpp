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
    QCoreApplication::setApplicationName(QStringLiteral("RuTool 2 Preview"));
    QCoreApplication::setOrganizationName(QStringLiteral("RuTool"));
    QCoreApplication::setApplicationVersion(QStringLiteral(RUTOOL_APP_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/assets/RuTool.ico")));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    ClientController controller;
    UiState uiState(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("settings.ini")), true);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &uiState, &UiState::saveWindow);
    QQmlApplicationEngine engine;
#ifdef RUTOOL_NATIVE_PAYLOAD
    engine.addImportPath(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("app/qml")));
#endif
    QObject::connect(&engine, &QQmlEngine::warnings, &app, [&controller](const QList<QQmlError>& errors) {
        for (const auto& error : errors) controller.recordRuntimeError(error.toString());
    });
    engine.rootContext()->setContextProperty(QStringLiteral("client"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("uiState"), &uiState);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(2); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("RuTool.App"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) return 2;
    return app.exec();
}
