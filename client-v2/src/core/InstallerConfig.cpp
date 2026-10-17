#include <rutool/core/InstallerConfig.hpp>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace rutool {
QString installerConfigPath(const QString& name) {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("app/config/") + name);
}
Result<platform::Secret> readPackagePassword(const QString& path) {
    const auto invalid = [&path] { return std::unexpected(Error{ErrorCode::InvalidInput,
        QStringLiteral("Не удалось прочитать пароль .rut. Проверьте файл app/config/installer-keys.json."), path}); };
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
