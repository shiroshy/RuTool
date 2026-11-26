#pragma once
#include <rutool/core/RutProtocol.hpp>
#include <QStringList>
#include <memory>

class QTemporaryDir;

namespace rutool::platform {
struct Startup {
    QString applicationName;
    QStringList qmlImportPaths;
    QString appImageHash{};
};
// Called before QGuiApplication; implemented by the selected platform.
Startup prepareStartup();
// Owns the temporary data for the entire offline UI check.
Result<std::unique_ptr<QTemporaryDir>> prepareSmokeData();
}
