#include "ClientController.hpp"
#include "UiState.hpp"
#include "Startup.hpp"
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
#include <QDebug>

#ifdef RUTOOL_NATIVE_PAYLOAD
extern "C" __declspec(dllexport) int rutoolMain(int argc, char* argv[]) {
#else
int main(int argc, char* argv[]) {
#endif
    const auto startup = rutool::platform::prepareStartup();
    QGuiApplication app(argc, argv);
    app.setProperty("rutoolAppImageHash", startup.appImageHash);
    QCoreApplication::setApplicationName(startup.applicationName);
    QCoreApplication::setOrganizationName(QStringLiteral("RuTool"));
    QCoreApplication::setApplicationVersion(QStringLiteral(RUTOOL_APP_DISPLAY_VERSION));
    QCommandLineParser parser;
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({QStringLiteral("smoke-test"), QStringLiteral("Load production QML offline with isolated temporary data, then exit.")});
    parser.addOption({QStringLiteral("smoke-screenshot"), QStringLiteral("Save the smoke-test window as PNG."), QStringLiteral("path")});
    parser.addOption({QStringLiteral("continue-update"), QStringLiteral("Continue a verified intermediate update chain."), QStringLiteral("hop")});
    parser.process(app);
    if (parser.isSet(QStringLiteral("continue-update"))) {
        bool valid{}; const int hop = parser.value(QStringLiteral("continue-update")).toInt(&valid);
        if (!valid || hop < 1 || hop > 8) return 3;
        app.setProperty("rutoolUpdateHop", hop);
    }
    const bool smoke = parser.isSet(QStringLiteral("smoke-test"));
    std::unique_ptr<QTemporaryDir> smokeData;
    if (smoke) {
        auto isolated = rutool::platform::prepareSmokeData();
        if (!isolated) { qCritical().noquote() << isolated.error().message; return 3; }
        smokeData = std::move(*isolated);
    }
    app.setProperty("rutoolSmokeTest", smoke);
    app.setWindowIcon(QIcon(QStringLiteral(":/assets/RuTool.ico")));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    ClientController controller;
    UiState uiState(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("settings.ini")), !smoke);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &uiState, &UiState::saveWindow);
    QQmlApplicationEngine engine;
    for (const auto& path : startup.qmlImportPaths) engine.addImportPath(path);
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
