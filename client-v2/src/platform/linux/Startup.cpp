#include "Startup.hpp"
#include <QTemporaryDir>
#include <rutool/core/Package.hpp>
#include <QFileInfo>

namespace rutool::platform {
Startup prepareStartup() {
    Startup startup{QStringLiteral("RuToolInstaller"), {}};
    const auto image = QFileInfo(QString::fromLocal8Bit(qgetenv("APPIMAGE"))).canonicalFilePath();
    if (!image.isEmpty() && QFileInfo(image).isFile()) {
        const auto hash = hashFile(image);
        if (hash) startup.appImageHash = *hash;
    }
    return startup;
}
Result<std::unique_ptr<QTemporaryDir>> prepareSmokeData() {
    auto data = std::make_unique<QTemporaryDir>();
    if (!data->isValid())
        return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Не удалось создать временный каталог проверки интерфейса.")});
    const auto path = data->path().toUtf8();
    qputenv("XDG_DATA_HOME", path);
    qputenv("XDG_CONFIG_HOME", path);
    qputenv("XDG_CACHE_HOME", path);
    return data;
}
}
