#pragma once
#include <QObject>
#include <QFutureWatcher>
#include <rutool/core/RutInspection.hpp>
#include <rutool/network/ServiceClient.hpp>
#include "DiagnosticLog.hpp"
#include <rutool/core/Installation.hpp>
#include <rutool/network/Transfers.hpp>
#include <rutool/network/Premium.hpp>
#include <rutool/core/InstallerConfig.hpp>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>
#include <QSet>
#include <rutool/platform/FileOps.hpp>

class ClientController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString serviceStatus READ serviceStatus NOTIFY changed)
    Q_PROPERTY(QString releaseStatus READ releaseStatus NOTIFY changed)
    Q_PROPERTY(QString packageStatus READ packageStatus NOTIFY changed)
    Q_PROPERTY(QString packageHash READ packageHash NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canCancel READ canCancel NOTIFY changed)
    Q_PROPERTY(QString packagePath READ packagePath NOTIFY changed)
    Q_PROPERTY(QString packageName READ packageName NOTIFY changed)
    Q_PROPERTY(bool hasPackage READ hasPackage NOTIFY changed)
    Q_PROPERTY(bool packageError READ packageError NOTIFY changed)
    Q_PROPERTY(bool packagePending READ packagePending NOTIFY changed)
    Q_PROPERTY(bool packageDownloading READ packageDownloading NOTIFY changed)
    Q_PROPERTY(QString gameFolder READ gameFolder WRITE setGameFolder NOTIFY changed)
    Q_PROPERTY(QString gameState READ gameState NOTIFY changed)
    Q_PROPERTY(bool gameValid READ gameValid NOTIFY changed)
    Q_PROPERTY(bool hasDiagnosticErrors READ hasDiagnosticErrors NOTIFY changed)
    Q_PROPERTY(bool exportingLog READ exportingLog NOTIFY changed)
    Q_PROPERTY(bool checkingAttachments READ checkingAttachments NOTIFY changed)
    Q_PROPERTY(QString faqText READ faqText NOTIFY contentChanged)
    Q_PROPERTY(QVariantList credits READ credits NOTIFY contentChanged)
    Q_PROPERTY(QString licenseText READ licenseText NOTIFY contentChanged)
    Q_PROPERTY(QString licenseHash READ licenseHash NOTIFY contentChanged)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY changed)
    Q_PROPERTY(bool canRestore READ canRestore NOTIFY changed)
    Q_PROPERTY(QString operationStatus READ operationStatus NOTIFY changed)
    Q_PROPERTY(QVariantMap primaryRelease READ primaryRelease NOTIFY changed)
    Q_PROPERTY(QString boostyCode READ boostyCode WRITE setBoostyCode NOTIFY changed)
    Q_PROPERTY(QString premiumStatus READ premiumStatus NOTIFY changed)
    Q_PROPERTY(bool checkingPremium READ checkingPremium NOTIFY changed)
    Q_PROPERTY(bool premiumActive READ premiumActive NOTIFY changed)
    Q_PROPERTY(QVariantList premiumReleases READ premiumReleases NOTIFY changed)
    Q_PROPERTY(bool premiumMainAvailable READ premiumMainAvailable NOTIFY changed)
    Q_PROPERTY(QString installationCount READ installationCount NOTIFY changed)
    Q_PROPERTY(bool sendingReport READ sendingReport NOTIFY changed)
    Q_PROPERTY(QString reportCooldown READ reportCooldown NOTIFY changed)
    Q_PROPERTY(bool officialPackage READ officialPackage NOTIFY changed)
    Q_PROPERTY(QString packageSigner READ packageSigner NOTIFY changed)
    Q_PROPERTY(QString packageKind READ packageKind NOTIFY changed)
    Q_PROPERTY(QString applicationUpdateVersion READ applicationUpdateVersion NOTIFY changed)
    Q_PROPERTY(QString applicationVersion READ applicationVersion CONSTANT)
public:
    explicit ClientController(QObject* parent = nullptr);
    ~ClientController() override;
    const QString& serviceStatus() const { return serviceStatus_; }
    const QString& releaseStatus() const { return releaseStatus_; }
    const QString& packageStatus() const { return packageStatus_; }
    const QString& packageHash() const { return packageHash_; }
    bool busy() const { return busy_; }
    bool canCancel() const { return busy_ && cancellable_; }
    const QString& packagePath() const { return packagePath_; }
    const QString& packageName() const { return packageName_; }
    bool hasPackage() const { return !packagePath_.isEmpty(); }
    bool packageError() const { return packageError_; }
    bool packagePending() const { return packagePhase_ != PackagePhase::Idle; }
    bool packageDownloading() const { return packagePhase_ == PackagePhase::Downloading; }
    const QString& gameFolder() const { return gameFolder_; }
    QString gameState() const;
    bool gameValid() const { return gameValid_; }
    void setGameFolder(const QString& path);
    bool hasDiagnosticErrors() const { return hasErrors_; }
    bool exportingLog() const { return exportWatcher_.isRunning(); }
    bool checkingAttachments() const { return attachmentWatcher_.isRunning(); }
    void recordRuntimeError(const QString& detail);
    Q_INVOKABLE void chooseGameFolder(const QUrl& url);
    Q_INVOKABLE void refreshServices();
    Q_INVOKABLE void refreshContent();
    const QString& faqText() const { return faqText_; }
    const QVariantList& credits() const { return credits_; }
    const QString& licenseText() const { return licenseText_; }
    const QString& licenseHash() const { return licenseHash_; }
    Q_INVOKABLE void inspectPackage(const QUrl& url);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE QUrl suggestedLogFile() const;
    Q_INVOKABLE void exportLog(const QUrl& url);
    Q_INVOKABLE void checkReportAttachments(const QList<QUrl>& urls);
    Q_INVOKABLE void start(const QString& isolatedDataRoot = {});
    Q_INVOKABLE void loadCatalog();
    Q_INVOKABLE void downloadTranslation(const QString& releaseId = {});
    const QString& boostyCode() const { return boostyCode_; }
    void setBoostyCode(const QString& code);
    const QString& premiumStatus() const { return premiumStatus_; }
    bool checkingPremium() const { return premiumTimer_.isActive() || premiumWatcher_.isRunning(); }
    bool premiumActive() const { return premiumAccepted_ && premiumValidThrough_.isValid() && premiumValidThrough_ >= QDateTime::currentDateTimeUtc().date(); }
    const QVariantList& premiumReleases() const { return premiumReleases_; }
    bool premiumMainAvailable() const { return premiumMain_ && latestTranslation_ && premiumMain_->sha256 == latestTranslation_->sha256 && premiumMain_->bytes == latestTranslation_->bytes && premiumMain_->originalHash == latestTranslation_->originalHash; }
    Q_INVOKABLE void loadPremiumCatalog();
    Q_INVOKABLE void verifyBoostyCode();
    Q_INVOKABLE void installTranslation();
    Q_INVOKABLE void restoreFiles();
    Q_INVOKABLE void confirmAction(const QString& token, bool accepted);
    Q_INVOKABLE void submitReport(const QString& username, const QString& text, const QString& topic, const QList<QUrl>& attachments);
    bool canInstall() const { return session_ && gameValid_ && !busy_ && !restoreWatcher_.isRunning(); }
    bool canRestore() const { return restoreAvailable_ && gameValid_ && !busy_; }
    const QString& operationStatus() const { return operationStatus_; }
    const QVariantMap& primaryRelease() const { return primaryRelease_; }
    const QString& installationCount() const { return installationCount_; }
    bool sendingReport() const { return reportWatcher_.isRunning(); }
    QString reportCooldown() const;
    bool officialPackage() const { return session_ && session_->official; }
    QString packageSigner() const { return session_ ? session_->package.signature.value(QStringLiteral("SignerId")).toString() : QString{}; }
    QString packageKind() const { return session_ ? session_->package.bundle ? QStringLiteral("Общий пакет") : QStringLiteral("База данных") : QStringLiteral("Не проверен"); }
    QString applicationUpdateVersion() const { return applicationRelease_ ? applicationRelease_->version : QString{}; }
    QString applicationVersion() const { return QStringLiteral(RUTOOL_APP_VERSION); }
    Q_INVOKABLE void checkApplicationUpdate(bool candidate = false);
    Q_INVOKABLE void requestApplicationUpdate();
signals:
    void changed();
    void contentChanged();
    void logExported(const QUrl& url, bool ok, const QString& message);
    void reportAttachmentsChecked(const QList<QUrl>& urls, bool ok, const QString& message);
    void confirmationRequested(const QString& token, const QString& heading, const QString& message);
    void reportSent(bool accepted, const QString& message);
private:
    friend class ControllerTests;
    enum class PackagePhase { Idle, Downloading, Checking };
    struct Session { std::shared_ptr<QTemporaryDir> staging; rutool::PreparedPackage package; bool official{}, originalVerified{}, ownedDownload{}; QString trustDetail; std::shared_ptr<rutool::platform::FileLock> downloadLease; };
    struct Work { std::shared_ptr<Session> session; QString status, backup; bool installed{}, restored{}; std::vector<QString> mismatch; QString helper{}, plan{}; std::shared_ptr<rutool::platform::FileLock> helperLock{}, cacheLease{}; };
    struct UpdateInfo { std::optional<rutool::ApplicationRelease> release; QByteArray envelope; };
    struct RestoreWork { bool available{}; std::optional<QString> recoveredBackup; };
    void initializeOperationsConnections();
    void initializePremiumConnections();
    void startPremium();
    static rutool::Result<std::shared_ptr<Session>> prepare(const QString& path, const QString& keysPath, const rutool::Cancellation& cancel, rutool::InstallerApi* transport = nullptr);
    void runInstall(bool allowUnverified, std::vector<QString> approved = {});
    void runRestore();
    void refreshCount();
    void refreshRestore();
    void saveOperations();
    void cleanDownloads(const QString& preservedPath = {});
    void askConfirmation(int action, const QString& heading, const QString& message);
    void runApplicationUpdate();
    std::shared_ptr<Session> session_;
    rutool::Cancellation lifetime_{std::make_shared<std::atomic_bool>(false)};
    QFutureWatcher<rutool::Result<Work>> workWatcher_;
    QFutureWatcher<rutool::Result<rutool::TranslationRelease>> catalogWatcher_;
    QFutureWatcher<rutool::Result<rutool::PremiumStatus>> premiumWatcher_;
    QFutureWatcher<rutool::Result<rutool::PremiumCatalogInfo>> premiumCatalogWatcher_;
    QTimer premiumTimer_;
    QString boostyCode_, checkingBoostyCode_;
    QString premiumStatus_{QStringLiteral("Код бусти открывает тестовые переводы Premium.")};
    bool premiumAccepted_{};
    QDate premiumValidThrough_;
    std::vector<rutool::TranslationRelease> premiumTranslations_;
    std::optional<rutool::TranslationRelease> premiumMain_;
    QVariantList premiumReleases_;
    QFutureWatcher<rutool::Result<qint64>> countWatcher_;
    QFutureWatcher<rutool::Result<RestoreWork>> restoreWatcher_;
    QFutureWatcher<rutool::Result<rutool::ReportResponse>> reportWatcher_;
    QFutureWatcher<rutool::Result<UpdateInfo>> updateWatcher_;
    QFuture<void> cacheCleanup_;
    std::shared_ptr<rutool::platform::FileLock> selectedDownloadLease_;
    QTimer cooldownTimer_;
    QJsonObject operationalSettings_;
    QString dataRoot_;
    QString backupPath_;
    QString confirmationToken_;
    int confirmationAction_{};
    std::vector<QString> approvedBefore_;
    bool operationalStarted_{};
    bool countRegistering_{};
    bool restoreAvailable_{};
    bool restoreAgain_{};
    bool cancellable_{true};
    bool installingUnverified_{};
    QVariantMap primaryRelease_;
    std::optional<rutool::TranslationRelease> latestTranslation_;
    std::optional<rutool::ApplicationRelease> applicationRelease_;
    QByteArray applicationEnvelope_;
    QString operationStatus_;
    QString installationCount_{QStringLiteral("—")};
    void logError(const QString& operation, const QString& code, const QString& message, const QString& details = {});
    DiagnosticLog diagnostic_;
    QFutureWatcher<QString> exportWatcher_;
    QFutureWatcher<QString> attachmentWatcher_;
    QList<QUrl> attachmentUrls_;
    QUrl exportUrl_;
    bool hasErrors_{};
    rutool::ServiceClient services_;
    QFutureWatcher<rutool::Result<rutool::RutInspection>> watcher_;
    std::shared_ptr<std::atomic_bool> cancel_;
    bool busy_{};
    bool packageError_{};
    PackagePhase packagePhase_{PackagePhase::Idle};
    bool gameValid_{};
    QString packagePath_;
    QString packageName_;
    QString gameFolder_;
    QString serviceStatus_{QStringLiteral("Нажмите «Проверить связь»")};
    QString releaseStatus_{QStringLiteral("Анонс ещё не проверен")};
    QString packageStatus_{QStringLiteral("Выберите .rut для проверки структуры")};
    QString packageHash_;
    qint64 contentRevision_{};
    QVariantList credits_;
    QString faqText_{QStringLiteral("FAQ пока не заполнен. Текст будет опубликован администратором.")};
    QString licenseText_;
    QString licenseHash_;
};
