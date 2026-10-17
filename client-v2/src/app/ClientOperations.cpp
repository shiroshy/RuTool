#include "ClientController.hpp"
#include "CacheCleanup.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QRegularExpression>
#include <QtConcurrent>

namespace {
QString operationalRoot() { return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(QStringLiteral("RuToolInstaller")); }
QJsonObject settingsFile(const QString& path) { QFile file(path); if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024) return {}; return QJsonDocument::fromJson(file.readAll()).object(); }
rutool::Error failure(const QString& message) { return {rutool::ErrorCode::InvalidInput, message}; }
}
void ClientController::initializeOperationsConnections() {
    connect(&workWatcher_, &QFutureWatcherBase::finished, this, [this] {
        const auto result = workWatcher_.result(); busy_ = false; cancellable_ = true;
        const bool packageWork = packagePending();
        const bool downloaded = packageWork && (packageDownloading() || (packagePath_.isEmpty() && !packageName_.isEmpty()));
        packagePhase_ = PackagePhase::Idle;
        if (!result) {
            packageError_ = result.error().code != rutool::ErrorCode::Cancelled; operationStatus_ = result.error().message;
            if (packageWork) {
                packageStatus_ = result.error().message; packageHash_.clear();
                if (downloaded || result.error().code == rutool::ErrorCode::Cancelled) { packagePath_.clear(); packageName_.clear(); session_.reset(); }
                if (result.error().code == rutool::ErrorCode::Cancelled) {
                    selectedDownloadLease_.reset();
                    operationalSettings_[QStringLiteral("PackagePath")] = QString{};
                    operationalSettings_[QStringLiteral("PendingDownloadedPackage")] = false;
                    saveOperations();
                }
                cleanDownloads(packagePath_);
            }
            if (packageError_) logError(QStringLiteral("Operation"), QStringLiteral("NATIVE"), result.error().message, result.error().details);
        }
        else {
            operationStatus_ = result->status;
            if (!result->helper.isEmpty()) {
                auto started = rutool::platform::launchHidden(result->helper, {QStringLiteral("--plan"), result->plan}, QCoreApplication::applicationDirPath());
                if (!started) logError(QStringLiteral("Updater"), QStringLiteral("START"), started.error().message);
                else { QCoreApplication::quit(); return; }
            }
            if (result->session) {
                session_ = result->session; packagePath_ = QDir::toNativeSeparators(session_->package.source); packageHash_ = session_->package.sourceHash; packageStatus_ = session_->trustDetail; packageError_ = false;
                operationalSettings_[QStringLiteral("PackagePath")] = session_->package.source;
                operationalSettings_[QStringLiteral("PendingDownloadedPackage")] = session_->ownedDownload;
                if (latestTranslation_ && session_->package.sourceHash == latestTranslation_->sha256) operationalSettings_[QStringLiteral("LatestTranslationSequence")] = latestTranslation_->sequence;
                saveOperations();
            }
            if (!result->mismatch.empty()) {
                approvedBefore_ = result->mismatch;
                askConfirmation(2, QStringLiteral("Версии файлов не совпадают"), QStringLiteral("Файлы игры отличаются от версии, для которой подготовлен перевод. Принудительная установка может не работать после обновления игры. Изменённые файлы не попадут в резервные копии оригинала. Продолжить?"));
            }
            if (result->installed || result->restored) {
                backupPath_ = result->backup; operationalSettings_[QStringLiteral("BackupPath")] = backupPath_;
                if (result->installed && !operationalSettings_.value(QStringLiteral("InstallationRegistered")).toBool()) operationalSettings_[QStringLiteral("InstallationPending")] = true;
                operationalSettings_[QStringLiteral("PackagePath")] = QString{}; if (session_) session_->downloadLease.reset(); session_.reset(); selectedDownloadLease_.reset(); packagePath_.clear(); packageName_.clear(); packageHash_.clear();
                operationalSettings_[QStringLiteral("PendingDownloadedPackage")] = false;
                saveOperations(); cleanDownloads(); refreshRestore(); refreshCount();
            }
        }
        emit changed();
    });
    connect(&catalogWatcher_, &QFutureWatcherBase::finished, this, [this] {
        const auto result = catalogWatcher_.result();
        if (result) {
            latestTranslation_ = *result;
            primaryRelease_ = {{QStringLiteral("id"), result->id}, {QStringLiteral("name"), QStringLiteral("Перевод Blue Archive · %1").arg(result->version)},
                {QStringLiteral("author"), result->author}, {QStringLiteral("publishedAt"), result->published.toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm"))}};
        } else { operationStatus_ = result.error().message; logError(QStringLiteral("Catalog"), QStringLiteral("NETWORK"), result.error().message); }
        emit changed();
    });
    connect(&countWatcher_, &QFutureWatcherBase::finished, this, [this] {
        const auto result = countWatcher_.result();
        if (result) { installationCount_ = QString::number(*result); if (countRegistering_) { operationalSettings_[QStringLiteral("InstallationPending")] = false; operationalSettings_[QStringLiteral("InstallationRegistered")] = true; saveOperations(); } }
        else logError(QStringLiteral("InstallationCount"), QStringLiteral("NETWORK"), result.error().message);
        emit changed();
    });
    connect(&restoreWatcher_, &QFutureWatcherBase::finished, this, [this] {
        if (restoreAgain_) { restoreAgain_ = false; refreshRestore(); }
        else {
            const auto result = restoreWatcher_.result();
            if (result) {
                restoreAvailable_ = result->available;
                if (result->recoveredBackup) { backupPath_ = *result->recoveredBackup; operationalSettings_[QStringLiteral("BackupPath")] = backupPath_; saveOperations(); }
            } else { restoreAvailable_ = false; operationStatus_ = result.error().message; logError(QStringLiteral("Recovery"), QStringLiteral("IO"), result.error().message, result.error().details); }
        }
        emit changed();
    });
    connect(&reportWatcher_, &QFutureWatcherBase::finished, this, [this] {
        const auto result = reportWatcher_.result();
        if (result) { operationalSettings_[QStringLiteral("ReportNextAllowedAtUtc")] = result->nextAllowed.toUTC().toString(Qt::ISODateWithMs); saveOperations(); emit reportSent(result->accepted, result->message); }
        else { logError(QStringLiteral("Report"), QStringLiteral("NETWORK"), result.error().message); emit reportSent(false, result.error().message); }
        emit changed();
    });
    connect(&cooldownTimer_, &QTimer::timeout, this, [this] { emit changed(); });
    connect(&updateWatcher_, &QFutureWatcherBase::finished, this, [this] {
        const auto result = updateWatcher_.result();
        if (result) { applicationRelease_ = result->release; applicationEnvelope_ = result->envelope; }
        else logError(QStringLiteral("ApplicationUpdate"), QStringLiteral("CHECK"), result.error().message);
        emit changed();
    });
}
void ClientController::start(const QString& isolatedDataRoot) {
    if (operationalStarted_) return;
    operationalStarted_ = true; dataRoot_ = isolatedDataRoot.isEmpty() ? operationalRoot() : QFileInfo(isolatedDataRoot).absoluteFilePath();
    const auto nativeSettings = QDir(dataRoot_).filePath(QStringLiteral("client-v2-settings.json"));
    if (QFileInfo::exists(nativeSettings)) {
        QFile settings(nativeSettings); QJsonParseError parseError;
        const bool readable = rutool::platform::requireSafeFile(nativeSettings).has_value() && settings.open(QIODevice::ReadOnly) && settings.size() <= 64 * 1024;
        const auto document = readable ? QJsonDocument::fromJson(settings.readAll(), &parseError) : QJsonDocument{}; settings.close();
        if (!readable || parseError.error != QJsonParseError::NoError || !document.isObject()) {
            const auto preserved = nativeSettings + QStringLiteral(".invalid-") + QUuid::createUuid().toString(QUuid::Id128);
            if (!readable || !QFile::copy(nativeSettings, preserved)) { operationalStarted_ = false; logError(QStringLiteral("Settings"), QStringLiteral("IO"), QStringLiteral("Настройки недоступны; исходный файл сохранён без изменений.")); return; }
            logError(QStringLiteral("Settings"), QStringLiteral("JSON"), QStringLiteral("Повреждённые настройки сохранены отдельно. Загружены прежние настройки."));
            operationalSettings_ = settingsFile(QDir(dataRoot_).filePath(QStringLiteral("settings.json")));
        } else operationalSettings_ = document.object();
    } else
    operationalSettings_ = settingsFile(QFileInfo::exists(nativeSettings) ? nativeSettings : QDir(dataRoot_).filePath(QStringLiteral("settings.json")));
    const auto selectedDownload = operationalSettings_.value(QStringLiteral("PackagePath")).toString();
    if (isTranslationDownload(dataRoot_, selectedDownload)) {
        auto lease = rutool::platform::FileLock::cacheEntry(selectedDownload);
        if (lease) selectedDownloadLease_ = std::move(*lease);
    }
    cleanDownloads(selectedDownload);
    QFile updateResult(QDir(dataRoot_).filePath(QStringLiteral("native-update-result.txt")));
    if (updateResult.open(QIODevice::ReadOnly) && updateResult.size() <= 64 * 1024) {
        const auto bytes = updateResult.readAll(); const auto digest = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        if (operationalSettings_.value(QStringLiteral("UpdateResultSha256")).toString() != digest) {
            operationalSettings_[QStringLiteral("UpdateResultSha256")] = digest;
            if (bytes.startsWith("ERROR:")) { operationStatus_ = QStringLiteral("Предыдущее обновление не завершилось. Сохраните подробный лог."); logError(QStringLiteral("Updater"), QStringLiteral("APPLY"), operationStatus_, QString::fromUtf8(bytes)); }
        }
    }
    backupPath_ = operationalSettings_.value(QStringLiteral("BackupPath")).toString();
    static const QRegularExpression uuidN(QStringLiteral("^[0-9a-fA-F]{32}$"));
    if (!uuidN.match(operationalSettings_.value(QStringLiteral("ReportClientId")).toString()).hasMatch()) operationalSettings_[QStringLiteral("ReportClientId")] = QUuid::createUuid().toString(QUuid::Id128);
    setGameFolder(operationalSettings_.value(QStringLiteral("GameFolder")).toString()); refreshRestore(); saveOperations();
    if (!busy_ && QFileInfo::exists(selectedDownload)) inspectPackage(QUrl::fromLocalFile(selectedDownload));
    cooldownTimer_.start(1000); loadCatalog(); refreshCount(); checkApplicationUpdate(); emit changed();
}
void ClientController::cleanDownloads(const QString& preservedPath) {
    if (dataRoot_.isEmpty()) return;
    auto previous = cacheCleanup_;
    cacheCleanup_ = QtConcurrent::run([previous, root = dataRoot_, preservedPath]() mutable {
        previous.waitForFinished();
        cleanupTranslationDownloads(root, preservedPath);
        cleanupAbandonedUpdates(root);
    });
}
void ClientController::saveOperations() {
    if (!operationalStarted_ || dataRoot_.isEmpty()) return;
    operationalSettings_[QStringLiteral("GameFolder")] = gameFolder_; QDir().mkpath(dataRoot_);
    QSaveFile file(QDir(dataRoot_).filePath(QStringLiteral("client-v2-settings.json"))); const auto bytes = QJsonDocument(operationalSettings_).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) logError(QStringLiteral("Settings"), QStringLiteral("IO"), QStringLiteral("Не удалось сохранить настройки нового клиента."), file.errorString());
}
void ClientController::refreshCount() {
    if (!operationalStarted_ || countWatcher_.isRunning()) return;
    countRegistering_ = operationalSettings_.value(QStringLiteral("InstallationPending")).toBool(); const auto registering = countRegistering_; const auto cancel = lifetime_;
    countWatcher_.setFuture(QtConcurrent::run([registering, cancel]() -> rutool::Result<qint64> {
        rutool::InstallerApi api(cancel); rutool::Result<QJsonObject> response;
        if (registering) { auto device = rutool::platform::installationDeviceId(); if (!device) return std::unexpected(device.error()); response = api.post(QStringLiteral("api/installer/installations"), {{QStringLiteral("deviceHash"), *device}}); }
        else response = api.get(QStringLiteral("api/installer/installations"));
        if (!response) return std::unexpected(response.error());
        const auto count = response->value(QStringLiteral("count")).toInteger(-1);
        if (count < 0) return std::unexpected(failure(QStringLiteral("Некорректный счётчик установок.")));
        return count;
    }));
}
void ClientController::refreshRestore() {
    if (restoreWatcher_.isRunning()) { restoreAgain_ = true; restoreAvailable_ = false; return; }
    const auto game = gameFolder_, backup = backupPath_; const auto cancel = lifetime_; restoreAvailable_ = false;
    restoreWatcher_.setFuture(QtConcurrent::run([game, backup, cancel]() -> rutool::Result<RestoreWork> {
        if (game.isEmpty()) return RestoreWork{};
        auto recovered = rutool::recoverInstallation(game); if (!recovered) return std::unexpected(recovered.error());
        const auto selected = recovered->has_value() ? **recovered : backup;
        return RestoreWork{!selected.isEmpty() && rutool::originalReceipt(game, selected, cancel).has_value(), *recovered};
    }));
}
void ClientController::loadCatalog() {
    if (catalogWatcher_.isRunning()) return;
    const auto minimum = operationalSettings_.value(QStringLiteral("LatestTranslationSequence")).toInteger(0); const auto cancel = lifetime_;
    catalogWatcher_.setFuture(QtConcurrent::run([minimum, cancel]() -> rutool::Result<rutool::TranslationRelease> {
        rutool::InstallerApi api(cancel); auto response = api.get(QStringLiteral("api/installer/releases/latest")); if (!response) return std::unexpected(response.error());
        return rutool::parseTranslationRelease(*response, minimum);
    }));
}
rutool::Result<std::shared_ptr<ClientController::Session>> ClientController::prepare(const QString& path, const QString& keysPath, const rutool::Cancellation& cancel) {
    auto framing = rutool::inspectRut(path, cancel); if (!framing) return std::unexpected(framing.error());
    auto password = rutool::readPackagePassword(keysPath); if (!password) return std::unexpected(password.error());
    rutool::InstallerApi api(cancel);
    auto session = std::make_shared<Session>(); session->staging = std::make_shared<QTemporaryDir>();
    if (!session->staging->isValid()) return std::unexpected(failure(QStringLiteral("Не удалось создать временный каталог пакета.")));
    auto package = rutool::decryptPackage(path, session->staging->path(), *password, cancel); if (!package) return std::unexpected(package.error());
    if (package->sourceHash != framing->sha256) return std::unexpected(failure(QStringLiteral("Пакет изменился во время чтения.")));
    session->package = *package; auto verified = api.verifyPackage(*package); session->official = verified && *verified;
    auto latest = api.get(QStringLiteral("api/installer/releases/latest"));
    if (latest) {
        auto publication = rutool::parseTranslationRelease(*latest, 0);
        if (publication && publication->originalHash == package->originalHash && (!package->bundle || session->official)) {
            auto trusted = api.post(QStringLiteral("api/verify"), publication->attestation);
            session->originalVerified = trusted && rutool::apiValue(*trusted, QStringLiteral("Valid")).toBool();
        }
    }
    session->trustDetail = session->official ? QStringLiteral("Официальный перевод команды · %1").arg(package->signature.value(QStringLiteral("SignerId")).toString()) : package->signature.isEmpty() ? QStringLiteral("Пакет без подписи команды") : QStringLiteral("Подпись не подтверждена сервером");
    return session;
}
void ClientController::downloadTranslation() {
    if (busy_ || !latestTranslation_) return;
    if (session_) session_->downloadLease.reset();
    session_.reset(); selectedDownloadLease_.reset(); confirmationToken_.clear(); packageError_ = false; packageHash_.clear(); packagePath_.clear();
    operationalSettings_[QStringLiteral("PackagePath")] = QString{};
    operationalSettings_[QStringLiteral("PendingDownloadedPackage")] = false;
    saveOperations(); cleanDownloads();
    const auto release = *latestTranslation_; const auto keysPath = rutool::installerConfigPath(QStringLiteral("installer-keys.json"));
    const auto cache = QDir(dataRoot_).filePath(QStringLiteral("TranslationDownloads")); const auto path = QDir(cache).filePath(QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".rut"));
    busy_ = true; cancellable_ = true; cancel_ = std::make_shared<std::atomic_bool>(false); const auto cancellation = cancel_;
    packageName_ = QStringLiteral("Blue Archive · %1").arg(release.version);
    packagePhase_ = PackagePhase::Downloading;
    packageStatus_ = QStringLiteral("Проверка целостности и подписи начнётся после завершения скачивания.");
    operationStatus_ = QStringLiteral("Скачиваем перевод…"); emit changed();
    workWatcher_.setFuture(QtConcurrent::run([this, release, keysPath, path, cancellation]() -> rutool::Result<Work> {
        auto password = rutool::readPackagePassword(keysPath); if (!password) return std::unexpected(password.error());
        rutool::InstallerApi api(cancellation);
        if (!release.attestation.isEmpty()) { auto verified = api.post(QStringLiteral("api/verify"), release.attestation); if (!verified || !rutool::apiValue(*verified, QStringLiteral("Valid")).toBool()) return std::unexpected(failure(QStringLiteral("Подпись публикации .rut не подтверждена."))); }
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return std::unexpected(failure(QStringLiteral("Не удалось создать кэш загрузок.")));
        auto lease = rutool::platform::FileLock::cacheEntry(path); if (!lease) return std::unexpected(lease.error());
        auto downloaded = rutool::downloadAsset(release.url, release.bytes, release.sha256, path, cancellation, [this](qint64 done, qint64 total) {
            QMetaObject::invokeMethod(this, [this, done, total] { operationStatus_ = QStringLiteral("Скачиваем перевод: %1 / %2 МБ").arg(done / (1024 * 1024)).arg(total / (1024 * 1024)); emit changed(); }, Qt::QueuedConnection);
        });
        if (!downloaded) return std::unexpected(downloaded.error());
        QMetaObject::invokeMethod(this, [this, cancellation] {
            if (cancel_ != cancellation || !busy_ || !packageDownloading()) return;
            packagePhase_ = PackagePhase::Checking; packageStatus_ = QStringLiteral("Файл скачан. Проверяем целостность и подпись автора…");
            operationStatus_ = QStringLiteral("Проверяем скачанный перевод…"); emit changed();
        }, Qt::QueuedConnection);
        auto selected = prepare(*downloaded, keysPath, cancellation);
        if (!selected || !(*selected)->official || (*selected)->package.originalHash != release.originalHash) { QFile::remove(path); return std::unexpected(selected ? failure(QStringLiteral("Скачанный перевод не прошёл проверку подписи или исходной версии.")) : selected.error()); }
        (*selected)->ownedDownload = true;
        (*selected)->downloadLease = std::move(*lease);
        return Work{*selected, QStringLiteral("Перевод проверен. Нажмите «Установить перевод»."), {}, false, false, {}};
    }));
}
void ClientController::askConfirmation(int action, const QString& heading, const QString& message) {
    confirmationToken_ = QUuid::createUuid().toString(QUuid::Id128); confirmationAction_ = action; emit confirmationRequested(confirmationToken_, heading, message);
}
void ClientController::installTranslation() {
    if (!canInstall()) return;
    if (!session_->official) { askConfirmation(1, QStringLiteral("Непроверенный перевод"), QStringLiteral("Подлинность этого перевода не подтверждена. При установке неподписанного или повреждённого пакета возможны проблемы. Всё равно установить?")); return; }
    runInstall(false);
}
void ClientController::runInstall(bool unverified, std::vector<QString> approved) {
    if (!canInstall() || (!session_->official && !unverified)) return;
    const auto session = session_; const auto game = gameFolder_, previous = backupPath_, backupRoot = QDir(dataRoot_).filePath(QStringLiteral("Backups"));
    installingUnverified_ = unverified; busy_ = true; cancellable_ = false; operationStatus_ = QStringLiteral("Устанавливаем перевод…"); cancel_ = std::make_shared<std::atomic_bool>(false); const auto cancel = cancel_;
    emit changed();
    workWatcher_.setFuture(QtConcurrent::run([session, game, previous, backupRoot, approved, cancel]() -> rutool::Result<Work> {
        auto observed = rutool::installationHashes(game, session->package.bundle, cancel); if (!observed) return std::unexpected(observed.error());
        std::vector<QString> originals{session->package.originalHash}, patched{session->package.patchedHash};
        if (session->package.bundle) { originals.push_back(session->package.bundleHashes[3]); originals.push_back(session->package.bundleHashes[4]); patched.push_back(session->package.bundleHashes[1]); patched.push_back(session->package.bundleHashes[2]); }
        if (approved.empty() && *observed != originals && *observed != patched) return Work{{}, {}, {}, false, false, *observed};
        rutool::InstallationRequest request{game, session->package, backupRoot, previous, approved, approved.empty() ? QString{} : session->package.patchedHash};
        request.originalVerified = session->originalVerified;
        auto installed = rutool::installPackage(request, []() -> rutool::Result<void> { return {}; }, cancel);
        if (!installed) return std::unexpected(installed.error());
        if (session->ownedDownload) QFile::remove(session->package.source);
        return Work{{}, installed->alreadyInstalled ? QStringLiteral("Этот перевод уже установлен. Файлы игры не изменены.") : installed->originalBackup.isEmpty() ? QStringLiteral("Перевод установлен. Для возврата оригинала используйте проверку Steam.") : QStringLiteral("Перевод установлен. Исходная копия сохранена."), installed->originalBackup, true, false, {}};
    }));
}
void ClientController::restoreFiles() { if (canRestore()) askConfirmation(3, QStringLiteral("Восстановление"), QStringLiteral("Восстановить подтверждённые исходные файлы игры?")); }
void ClientController::runRestore() {
    if (!canRestore()) return;
    const auto game = gameFolder_, backup = backupPath_;
    busy_ = true; cancellable_ = false; operationStatus_ = QStringLiteral("Восстанавливаем исходные файлы…"); emit changed();
    workWatcher_.setFuture(QtConcurrent::run([game, backup]() -> rutool::Result<Work> {
        auto restored = rutool::restoreOriginal(game, backup); if (!restored) return std::unexpected(restored.error());
        return Work{{}, QStringLiteral("Исходные файлы восстановлены."), {}, false, true, {}};
    }));
}
void ClientController::confirmAction(const QString& token, bool accepted) {
    if (token.isEmpty() || token != confirmationToken_ || busy_) return;
    const auto action = confirmationAction_; confirmationToken_.clear(); confirmationAction_ = 0;
    if (!accepted) { approvedBefore_.clear(); operationStatus_ = QStringLiteral("Операция отменена."); emit changed(); return; }
    if (action == 1) runInstall(true); else if (action == 2) { auto approved = std::move(approvedBefore_); runInstall(installingUnverified_, std::move(approved)); } else if (action == 3) runRestore(); else if (action == 4) runApplicationUpdate();
}
QString ClientController::reportCooldown() const {
    const auto next = QDateTime::fromString(operationalSettings_.value(QStringLiteral("ReportNextAllowedAtUtc")).toString(), Qt::ISODateWithMs); const auto seconds = QDateTime::currentDateTimeUtc().secsTo(next);
    return next.isValid() && seconds > 0 ? QStringLiteral("Следующий репорт через %1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0')) : QString{};
}
void ClientController::submitReport(const QString& username, const QString& text, const QString& topic, const QList<QUrl>& urls) {
    if (!operationalStarted_ || sendingReport()) return;
    if (!reportCooldown().isEmpty()) { emit reportSent(false, reportCooldown()); return; }
    const rutool::ReportRequest request{operationalSettings_.value(QStringLiteral("ReportClientId")).toString(), username, text, topic, urls}; const auto cancel = lifetime_;
    reportWatcher_.setFuture(QtConcurrent::run([request, cancel] { return rutool::sendReport(request, cancel); })); emit changed();
}
