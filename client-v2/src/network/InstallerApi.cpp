#include <rutool/network/InstallerApi.hpp>
#include <QCryptographicHash>
#include <QDateTime>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QThread>
#include <QCoreApplication>
#include <QTimer>
#include <QUrl>

namespace rutool {
namespace {
Error networkError(const QString& text) { return {ErrorCode::Network, text}; }
}
QJsonValue apiValue(const QJsonObject& object, const QString& name) {
    if (object.contains(name)) return object.value(name);
    auto lower = name; lower[0] = lower[0].toLower(); return object.value(lower);
}
InstallerApi::InstallerApi(Cancellation cancel) : cancel_(std::move(cancel)) {}
Result<QJsonObject> InstallerApi::get(const QString& endpoint) { return json(endpoint, {}, false); }
Result<QJsonObject> InstallerApi::post(const QString& endpoint, const QJsonObject& object) { return json(endpoint, QJsonDocument(object).toJson(QJsonDocument::Compact), true); }
Result<QJsonObject> InstallerApi::json(const QString& endpoint, const QByteArray& body, bool posting) {
    if (QThread::currentThread() == QCoreApplication::instance()->thread())
        return std::unexpected(networkError(QStringLiteral("Сетевую операцию нужно выполнять в рабочем потоке.")));
    const QStringList allowed{QStringLiteral("api/verify"), QStringLiteral("api/installer/releases/latest"),
        QStringLiteral("api/installer/installations"), QStringLiteral("api/installer/application/latest"), QStringLiteral("api/installer/application/candidate")};
    if (!allowed.contains(endpoint) || body.size() > 16 * 1024) return std::unexpected(networkError(QStringLiteral("Недопустимый запрос API.")));
    if (cancel_ && cancel_->load()) return std::unexpected(Error{ErrorCode::Cancelled, QStringLiteral("Операция отменена.")});
    QNetworkRequest request(QUrl(QStringLiteral("https://bluehalo.site/") + endpoint));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    auto* reply = posting ? manager_.post(request, body) : manager_.get(request);
    QEventLoop loop; QTimer deadline, cancellation; QByteArray bytes; bool oversized{}, timedOut{};
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] { timedOut = true; reply->abort(); });
    QObject::connect(&cancellation, &QTimer::timeout, &loop, [&] { if (cancel_ && cancel_->load()) reply->abort(); });
    QObject::connect(reply, &QIODevice::readyRead, &loop, [&] {
        const auto chunk = reply->read(1024 * 1024 + 1 - bytes.size()); bytes += chunk;
        if (bytes.size() > 1024 * 1024) { oversized = true; reply->abort(); }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    deadline.start(20000); cancellation.start(100); loop.exec();
    bytes += reply->read(1024 * 1024 + 1 - bytes.size());
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto error = reply->error(); delete reply;
    if (cancel_ && cancel_->load()) return std::unexpected(Error{ErrorCode::Cancelled, QStringLiteral("Операция отменена.")});
    if (status == 403) return std::unexpected(networkError(QStringLiteral("Сервер отклонил запрос (HTTP 403).")));
    if (error != QNetworkReply::NoError || status < 200 || status >= 300 || oversized || timedOut)
        return std::unexpected(networkError(QStringLiteral("Сервер недоступен или вернул некорректный ответ (HTTP %1).").arg(status)));
    if (status == 204 && bytes.isEmpty()) return QJsonObject{};
    QJsonParseError parse; const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return std::unexpected(networkError(QStringLiteral("Некорректный JSON сервера.")));
    return document.object();
}
Result<bool> InstallerApi::verifyPackage(const PreparedPackage& package) {
    if (package.signature.isEmpty()) return false;
    auto response = post(QStringLiteral("api/verify"), package.signature);
    if (!response) return std::unexpected(response.error());
    return apiValue(*response, QStringLiteral("Valid")).toBool(false);
}
}
