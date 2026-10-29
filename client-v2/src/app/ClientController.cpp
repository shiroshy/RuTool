#include "ClientController.hpp"
#include "CacheCleanup.hpp"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QRegularExpression>
#include <rutool/network/ClientContent.hpp>

ClientController::ClientController(QObject* parent) : QObject(parent), services_(rutool::updatePublicKey(), this) {
    initializeOperationsConnections();
    initializePremiumConnections();
    credits_ = rutool::defaultCredits();
    QFile license(QStringLiteral(":/assets/License.txt"));
    if (license.open(QIODevice::ReadOnly)) licenseText_ = QString::fromUtf8(license.readAll());
    licenseHash_ = QString::fromLatin1(QCryptographicHash::hash(licenseText_.toUtf8(), QCryptographicHash::Sha256).toHex());
    const auto cachePath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("client-content.json"));
    QFile cache(cachePath);
    if (cache.open(QIODevice::ReadOnly) && cache.size() <= 384 * 1024) {
        const auto content = rutool::parseClientContent(cache.readAll());
        if (content) { contentRevision_ = content->revision; faqText_ = content->faq; credits_ = content->credits; }
    }
    connect(&services_, &rutool::ServiceClient::contentChecked, this, [this, cachePath](bool ok, const QByteArray& json, const QString&) {
        if (!ok) return;
        const auto content = rutool::parseClientContent(json);
        if (!content || content->revision < contentRevision_) return;
        contentRevision_ = content->revision; faqText_ = content->faq; credits_ = content->credits;
        QDir().mkpath(QFileInfo(cachePath).absolutePath()); QSaveFile cacheFile(cachePath);
        if (cacheFile.open(QIODevice::WriteOnly)) { if (cacheFile.write(json) == json.size()) cacheFile.commit(); else cacheFile.cancelWriting(); }
        emit contentChanged();
    });
    diagnostic_.record(QStringLiteral("Application"), QStringLiteral("START"), QStringLiteral("Новая сессия"));
    connect(&services_, &rutool::ServiceClient::healthChecked, this, [this](bool, const QString& detail) { serviceStatus_ = detail; emit changed(); });
    connect(&services_, &rutool::ServiceClient::releaseChecked, this, [this](bool ok, const QString& version, const QString& detail) {
        releaseStatus_ = ok && !version.isEmpty() ? QStringLiteral("Installer %1 · ").arg(version) + detail : detail; emit changed();
    });
    connect(&watcher_, &QFutureWatcherBase::finished, this, [this] {
        const auto result = watcher_.result(); busy_ = false;
        packagePhase_ = PackagePhase::Idle;
        packageError_ = !result && result.error().code != rutool::ErrorCode::Cancelled;
        if (result) {
            packageStatus_ = QStringLiteral("RUT111 · %1 блоков · %2 МиБ\nПодлинность содержимого ещё не проверена")
                .arg(result->chunks).arg(static_cast<double>(result->bytes) / (1024.0 * 1024.0), 0, 'f', 2);
            packageHash_ = result->sha256;
        } else {
            packageStatus_ = result.error().message; packageHash_.clear();
            if (result.error().code == rutool::ErrorCode::Cancelled) { packagePath_.clear(); packageName_.clear(); }
            if (packageError_) logError(QStringLiteral("InspectRut"), QStringLiteral("RUT.%1").arg(static_cast<int>(result.error().code)),
                packageStatus_, QStringLiteral("File: %1\n%2").arg(QFileInfo(packagePath_).fileName(), result.error().details));
        }
        emit changed();
    });
    connect(&services_, &rutool::ServiceClient::diagnosticError, this, &ClientController::logError);
    connect(&exportWatcher_, &QFutureWatcherBase::finished, this, [this] {
        const auto error = exportWatcher_.result();
        if (!error.isEmpty()) logError(QStringLiteral("ExportLog"), QStringLiteral("IO"), QStringLiteral("Не удалось сохранить лог"), error);
        emit changed();
        emit logExported(exportUrl_, error.isEmpty(), error.isEmpty() ? QStringLiteral("Лог сохранён в .txt") : QStringLiteral("Не удалось сохранить лог: %1").arg(DiagnosticLog::sanitize(error)));
    });
    connect(&attachmentWatcher_, &QFutureWatcherBase::finished, this, [this] {
        const auto error = attachmentWatcher_.result();
        emit changed();
        emit reportAttachmentsChecked(attachmentUrls_, error.isEmpty(), error);
    });
}
ClientController::~ClientController() {
    lifetime_->store(true); if (cancel_) cancel_->store(true); services_.cancel(); cooldownTimer_.stop(); premiumTimer_.stop();
    watcher_.waitForFinished(); exportWatcher_.waitForFinished(); attachmentWatcher_.waitForFinished(); workWatcher_.waitForFinished();
    catalogWatcher_.waitForFinished(); countWatcher_.waitForFinished(); restoreWatcher_.waitForFinished(); reportWatcher_.waitForFinished(); updateWatcher_.waitForFinished();
    premiumWatcher_.waitForFinished(); premiumCatalogWatcher_.waitForFinished();
    cacheCleanup_.waitForFinished();
}
void ClientController::refreshServices() {
    serviceStatus_ = QStringLiteral("Проверяем HTTPS…"); releaseStatus_ = QStringLiteral("Проверяем подпись анонса…");
    emit changed(); services_.checkHealth(); services_.checkApplicationRelease();
}
void ClientController::refreshContent() { services_.checkClientContent(); }
void ClientController::inspectPackage(const QUrl& url) {
    if (busy_) return;
    const auto path = url.toLocalFile();
    if (!url.isLocalFile() || !path.endsWith(QStringLiteral(".rut"), Qt::CaseInsensitive)) {
        packageError_ = true; packageStatus_ = QStringLiteral("Выберите локальный файл .rut");
        logError(QStringLiteral("InspectRut"), QStringLiteral("INPUT"), packageStatus_); return;
    }
    if (session_) session_->downloadLease.reset();
    busy_ = true; packageError_ = false; packagePhase_ = PackagePhase::Checking; session_.reset(); confirmationToken_.clear();
    packagePath_ = QDir::toNativeSeparators(path); packageName_ = QFileInfo(path).completeBaseName();
    packageHash_.clear(); packageStatus_ = QStringLiteral("Читаем контейнер…");
    cancel_ = std::make_shared<std::atomic_bool>(false); const auto cancellation = cancel_;
    emit changed();
    if (operationalStarted_) {
        operationalSettings_.remove(QStringLiteral("PendingTransferPath")); operationalSettings_.remove(QStringLiteral("PendingTransferSha256"));
        const auto keysPath = rutool::installerConfigPath(QStringLiteral("installer-keys.json"));
        static const QRegularExpression downloadedName(QStringLiteral("^[0-9a-f]{32}[.]rut$"));
        const bool owned = operationalSettings_.value(QStringLiteral("PendingDownloadedPackage")).toBool() && QDir::fromNativeSeparators(operationalSettings_.value(QStringLiteral("PackagePath")).toString()) == QDir::fromNativeSeparators(path) && downloadedName.match(QFileInfo(path).fileName()).hasMatch() && QFileInfo(path).absolutePath().compare(QDir(dataRoot_).filePath(QStringLiteral("TranslationDownloads")), Qt::CaseInsensitive) == 0;
        if (QFileInfo(operationalSettings_.value(QStringLiteral("PackagePath")).toString()).absoluteFilePath().compare(QFileInfo(path).absoluteFilePath(), Qt::CaseInsensitive) != 0) selectedDownloadLease_.reset();
        rutool::Result<void> locked;
        if (isTranslationDownload(dataRoot_, path) && !selectedDownloadLease_) {
            auto lease = rutool::platform::FileLock::cacheEntry(path);
            if (lease) selectedDownloadLease_ = std::move(*lease); else locked = std::unexpected(lease.error());
        }
        operationalSettings_[QStringLiteral("PackagePath")] = path;
        operationalSettings_[QStringLiteral("PendingDownloadedPackage")] = owned;
        saveOperations(); cleanDownloads(path);
        const auto downloadLease = selectedDownloadLease_;
        workWatcher_.setFuture(QtConcurrent::run([path, keysPath, cancellation, owned, downloadLease, locked]() -> rutool::Result<Work> {
            if (!locked) return std::unexpected(locked.error());
            auto selected = prepare(path, keysPath, cancellation);
            if (!selected) return std::unexpected(selected.error());
            (*selected)->ownedDownload = owned;
            (*selected)->downloadLease = downloadLease;
            return Work{*selected, QStringLiteral("Пакет прочитан и проверен."), {}, false, false, {}};
        }));
        return;
    }
    watcher_.setFuture(QtConcurrent::run([path, cancellation] { return rutool::inspectRut(path, cancellation); }));
}
void ClientController::cancel() { if (cancellable_ && cancel_) cancel_->store(true); services_.cancel(); }
QString ClientController::gameState() const {
    return gameValid_ ? QStringLiteral("✓  Папка выбрана") : gameFolder_.isEmpty() ? QStringLiteral("Папка не выбрана") : QStringLiteral("Проверьте папку");
}
void ClientController::setGameFolder(const QString& path) {
    const auto normalized = path.trimmed().isEmpty() ? QString{} : QDir::toNativeSeparators(QDir::cleanPath(path.trimmed()));
    if (busy_ || gameFolder_ == normalized) return;
    if (operationalStarted_ && !gameFolder_.isEmpty()) saveOperations();
    const auto oldSettingsGame = operationalSettings_.value(QStringLiteral("GameFolder")).toString();
    gameFolder_ = normalized;
    if (operationalStarted_) {
        const auto key = QDir::fromNativeSeparators(QFileInfo(gameFolder_).absoluteFilePath()).toCaseFolded();
        const auto backups = operationalSettings_.value(QStringLiteral("GameBackups")).toObject();
        backupPath_ = backups.contains(key) ? backups.value(key).toString() :
            !oldSettingsGame.isEmpty() && QFileInfo(oldSettingsGame).absoluteFilePath().compare(QFileInfo(gameFolder_).absoluteFilePath(), Qt::CaseInsensitive) == 0
            ? operationalSettings_.value(QStringLiteral("BackupPath")).toString() : QString{};
    }
    const QFileInfo database(QDir(gameFolder_).filePath(
        QStringLiteral("BlueArchive_Data/StreamingAssets/PUB/Resource/Preload/TableBundles/ExcelDB.db")));
    gameValid_ = !gameFolder_.isEmpty() && database.isFile() && database.isReadable();
    if (!gameValid_ && !gameFolder_.isEmpty()) logError(QStringLiteral("GameFolder"), QStringLiteral("MISSING_DATABASE"), QStringLiteral("Ожидаемая база игры не найдена в выбранном каталоге"));
    if (operationalStarted_) { saveOperations(); refreshRestore(); }
    emit changed();
}
void ClientController::chooseGameFolder(const QUrl& url) {
    if (url.isLocalFile()) setGameFolder(QDir::toNativeSeparators(url.toLocalFile()));
}
void ClientController::logError(const QString& operation, const QString& code, const QString& message, const QString& details) {
    diagnostic_.record(operation, code, message, details); hasErrors_ = true; emit changed();
}
void ClientController::recordRuntimeError(const QString& detail) {
    logError(QStringLiteral("QML"), QStringLiteral("RUNTIME"), QStringLiteral("Ошибка интерфейса"), detail);
}
QUrl ClientController::suggestedLogFile() const {
    return QUrl::fromLocalFile(QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(
        QStringLiteral("RuTool-error-%1.txt").arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")))));
}
void ClientController::exportLog(const QUrl& url) {
    if (exportWatcher_.isRunning()) return;
    if (!url.isLocalFile()) { emit logExported(url, false, QStringLiteral("Выберите локальный .txt файл")); return; }
    exportUrl_ = url;
    const auto snapshot = diagnostic_.snapshot();
    const auto path = url.toLocalFile();
    exportWatcher_.setFuture(QtConcurrent::run([path, snapshot] {
        QString error; DiagnosticLog::writeSnapshot(path, snapshot, error); return error;
    }));
    emit changed();
}
void ClientController::checkReportAttachments(const QList<QUrl>& urls) {
    if (checkingAttachments()) return;
    attachmentUrls_ = urls;
    attachmentWatcher_.setFuture(QtConcurrent::run([urls] {
        const auto valid = rutool::validateReportAttachments(urls); return valid ? QString{} : valid.error().message;
    }));
    emit changed();
}
