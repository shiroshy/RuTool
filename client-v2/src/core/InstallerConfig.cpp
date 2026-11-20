#include <rutool/core/InstallerConfig.hpp>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace rutool {
QString installerConfigPath(const QString& name) {
#ifdef Q_OS_LINUX
    if (name == QStringLiteral("installer-keys.json")) {
        const auto overridePath = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath(QStringLiteral("rutool/installer-keys.json"));
        // An invalid override must report an error rather than silently use the bundled file.
        if (QFileInfo::exists(overridePath) || QFileInfo(overridePath).isSymLink()) return overridePath;
    }
#endif
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("app/config/") + name);
}
Result<platform::Secret> readPackagePassword(const QString& path) {
    const auto invalid = [&path] { return std::unexpected(Error{ErrorCode::InvalidInput,
        QStringLiteral("Не удалось прочитать пароль .rut. Проверьте файл конфигурации: %1").arg(path), path}); };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024) return invalid();
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return invalid();
    const auto object = document.object();
    if (object.value(QStringLiteral("FormatVersion")).toInt() != 1 || !object.value(QStringLiteral("RutPassword")).isString()) return invalid();
    auto password = object.value(QStringLiteral("RutPassword")).toString().toUtf8();
    if (password.isEmpty() || password.size() > 4096) return invalid();
    return platform::Secret(std::move(password));
}
QByteArray updatePublicKey() {
    QFile file(installerConfigPath(QStringLiteral("update-public.der")));
    return file.open(QIODevice::ReadOnly) && file.size() <= 4096 ? file.readAll() : QByteArray{};
}
}
