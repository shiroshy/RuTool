#include "ClientController.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <QVersionNumber>
#include <QUuid>
#include <QtConcurrent>

namespace {
rutool::Error updateError(const QString& message) { return {rutool::ErrorCode::Io, message}; }
bool save(const QString& path, const QByteArray& bytes) { QSaveFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(); }
}
void ClientController::checkApplicationUpdate(bool candidate) {
    if (updateWatcher_.isRunning()) return;
    const auto key = rutool::updatePublicKey(); const auto cancel = lifetime_;
    updateWatcher_.setFuture(QtConcurrent::run([key, cancel, candidate]() -> rutool::Result<UpdateInfo> {
        rutool::InstallerApi api(cancel); auto object = api.get(candidate ? QStringLiteral("api/installer/application/candidate") : QStringLiteral("api/installer/application/latest"));
        if (!object) return std::unexpected(object.error());
        if (object->isEmpty()) return UpdateInfo{};
        const auto envelope = QJsonDocument(*object).toJson(QJsonDocument::Compact); auto release = rutool::verifyApplicationEnvelope(envelope, key);
        if (!release) return std::unexpected(release.error());
        if (!release->version.startsWith(QStringLiteral("2.")) || QVersionNumber::fromString(release->version) <= QVersionNumber::fromString(QStringLiteral(RUTOOL_APP_VERSION))) return UpdateInfo{};
        return UpdateInfo{*release, envelope};
    }));
}
void ClientController::requestApplicationUpdate() {
    if (busy_ || !applicationRelease_) return;
    askConfirmation(4, QStringLiteral("Обновление RuTool"), QStringLiteral("Скачать новую версию программы %1? После проверки файлов RuTool закроется, обновится и запустится снова.").arg(applicationRelease_->version));
}
void ClientController::runApplicationUpdate() {
    if (busy_ || !applicationRelease_) return;
    const auto release = *applicationRelease_; const auto envelope = applicationEnvelope_;
    const auto root = QCoreApplication::applicationDirPath(), cache = QDir(dataRoot_).filePath(QStringLiteral("ApplicationUpdates/%1").arg(QUuid::createUuid().toString(QUuid::Id128)));
    cancel_ = std::make_shared<std::atomic_bool>(false); const auto cancellation = cancel_;
    busy_ = true; cancellable_ = true; operationStatus_ = QStringLiteral("Готовим обновление программы…"); emit changed();
    workWatcher_.setFuture(QtConcurrent::run([this, release, envelope, root, cache, cancellation]() -> rutool::Result<Work> {
        if (QVersionNumber::fromString(release.version) <= QVersionNumber::fromString(QStringLiteral(RUTOOL_APP_VERSION))) return std::unexpected(updateError(QStringLiteral("Обновление устарело.")));
        const auto stage = QDir(cache).filePath(QStringLiteral("stage")); if (!QDir().mkpath(stage)) return std::unexpected(updateError(QStringLiteral("Не удалось создать каталог обновления.")));
        auto cacheLease = rutool::platform::FileLock::directoryLease(cache); if (!cacheLease) return std::unexpected(cacheLease.error());
        auto started = rutool::platform::currentProcessStartedTicks(); if (!started) return std::unexpected(started.error());
        const QJsonObject owner{{QStringLiteral("ProcessId"), QCoreApplication::applicationPid()}, {QStringLiteral("ProcessStartedUtcTicks"), *started}};
        if (!save(QDir(cache).filePath(QStringLiteral("owner.json")), QJsonDocument(owner).toJson(QJsonDocument::Compact))) return std::unexpected(updateError(QStringLiteral("Не удалось сохранить владельца загрузки.")));
        for (const auto& file : release.files) {
            if (cancellation->load()) return std::unexpected(rutool::Error{rutool::ErrorCode::Cancelled, QStringLiteral("Обновление отменено.")});
            const auto source = QDir(root).filePath(file.path), destination = QDir(stage).filePath(file.path); QDir().mkpath(QFileInfo(destination).absolutePath());
            auto local = rutool::hashFile(source, cancellation);
            if (local && *local == file.sha256 && QFileInfo(source).size() == file.bytes) { if (!QFile::copy(source, destination)) return std::unexpected(updateError(QStringLiteral("Не удалось подготовить неизменённый файл обновления."))); }
            else {
                QMetaObject::invokeMethod(this, [this, file] { operationStatus_ = QStringLiteral("Скачиваем обновление: %1").arg(file.path); emit changed(); }, Qt::QueuedConnection);
                const auto url = QUrl(QStringLiteral("https://bluehalo.site/api/installer/application/assets/") + file.asset);
                auto downloaded = rutool::downloadAsset(url, file.bytes, file.sha256, destination, cancellation); if (!downloaded) return std::unexpected(downloaded.error());
            }
            auto hash = rutool::hashFile(destination, cancellation); if (!hash || *hash != file.sha256) return std::unexpected(updateError(QStringLiteral("Проверка подготовленного файла обновления не прошла.")));
        }
        if (!save(QDir(stage).filePath(QStringLiteral("Installer.release.json")), envelope)) return std::unexpected(updateError(QStringLiteral("Не удалось сохранить подписанный манифест обновления.")));
        const auto helperSource = QDir(root).filePath(QStringLiteral("updater/RuToolUpdater.exe")), helperDirectory = QDir(cache).filePath(QStringLiteral("helper")); QDir().mkpath(helperDirectory);
        const auto helper = QDir(helperDirectory).filePath(QStringLiteral("RuToolUpdater.exe"));
        if (!QFile::copy(helperSource, helper)) return std::unexpected(updateError(QStringLiteral("Обновлятор недоступен.")));
        auto helperLock = rutool::platform::FileLock::read(helper); if (!helperLock) return std::unexpected(helperLock.error());
        const auto plan = QDir(cache).filePath(QStringLiteral("plan.json")); auto object = owner; object[QStringLiteral("Root")] = root; object[QStringLiteral("Stage")] = stage;
        QJsonArray files;
        for (const auto& file : release.files) files.append(QJsonObject{{QStringLiteral("Path"), file.path}, {QStringLiteral("Bytes"), file.bytes}, {QStringLiteral("Sha256"), file.sha256}});
        const auto manifestHash = rutool::hashFile(QDir(stage).filePath(QStringLiteral("Installer.release.json")), cancellation);
        if (!manifestHash) return std::unexpected(manifestHash.error());
        files.append(QJsonObject{{QStringLiteral("Path"), QStringLiteral("Installer.release.json")}, {QStringLiteral("Bytes"), envelope.size()}, {QStringLiteral("Sha256"), *manifestHash}});
        object[QStringLiteral("Files")] = files;
        object[QStringLiteral("Restart")] = true;
        if (!save(plan, QJsonDocument(object).toJson(QJsonDocument::Compact))) return std::unexpected(updateError(QStringLiteral("Не удалось сохранить план обновления.")));
        Work result; result.helper = helper; result.plan = plan; result.helperLock = std::shared_ptr<rutool::platform::FileLock>(std::move(*helperLock)); result.cacheLease = std::shared_ptr<rutool::platform::FileLock>(std::move(*cacheLease)); result.status = QStringLiteral("Обновление проверено. Перезапускаем программу…"); return result;
    }));
}
