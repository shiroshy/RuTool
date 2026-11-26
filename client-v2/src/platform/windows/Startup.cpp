#include "Startup.hpp"
#include <rutool/platform/Runtime.hpp>
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

namespace rutool::platform {
Startup prepareStartup() {
    Startup startup{QStringLiteral("RuTool 2 Preview"), {}};
#ifdef RUTOOL_NATIVE_PAYLOAD
    const auto payload = QDir(applicationRoot()).filePath(QStringLiteral("app"));
    for (const auto* name : {"QT_QPA_PLATFORM_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "QT_QUICK_CONTROLS_CONF", "QML_DISK_CACHE_PATH"}) qunsetenv(name);
    const auto plugins = QDir(payload).filePath(QStringLiteral("plugins"));
    qputenv("QT_PLUGIN_PATH", plugins.toUtf8());
    QCoreApplication::setLibraryPaths({plugins});
    startup.qmlImportPaths.append(QDir(payload).filePath(QStringLiteral("qml")));
#endif
    return startup;
}
Result<std::unique_ptr<QTemporaryDir>> prepareSmokeData() {
    return std::unexpected(Error{ErrorCode::UnsupportedFormat,
        QStringLiteral("Изолированная проверка интерфейса пока реализована только для Linux.")});
}
}
