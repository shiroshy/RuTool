#pragma once
#include <rutool/network/InstallerApi.hpp>
#include <QUrl>
#include <QDateTime>

namespace rutool {
struct TranslationRelease {
    QString id, version, originalHash, sha256, author;
    QUrl url;
    qint64 bytes{}, sequence{};
    QDateTime published;
    QJsonObject attestation;
    QString name;
    bool premium{};
    bool main{};
};
Result<TranslationRelease> parseTranslationRelease(const QJsonObject& json, qint64 minimumSequence);
bool allowedAssetUrl(const QUrl& url);
using TransferProgress = std::function<void(qint64, qint64)>;
int downloadDeadlineMilliseconds(qint64 expectedBytes);
Result<QString> downloadAsset(const QUrl& url, qint64 expectedBytes, const QString& expectedHash,
                              const QString& destination, const Cancellation& cancel, const TransferProgress& progress = {}, QNetworkAccessManager* transport = nullptr, const QByteArray& premiumCode = {});
struct ReportRequest { QString clientId, username, text, topic; QList<QUrl> attachments; };
struct ReportResponse { QDateTime nextAllowed; bool accepted{}; QString message; };
Result<void> validateReportAttachments(const QList<QUrl>& attachments);
Result<ReportResponse> sendReport(const ReportRequest& request, const Cancellation& cancel, QNetworkAccessManager* transport = nullptr);
}
