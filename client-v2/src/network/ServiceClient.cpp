#include <rutool/network/ServiceClient.hpp>
#include <rutool/network/ClientContent.hpp>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <memory>

namespace rutool {
ServiceClient::ServiceClient(QByteArray updatePublicKey, QObject* parent)
    : ServiceClient(std::move(updatePublicKey), std::make_unique<QNetworkAccessManager>(), parent) {}
ServiceClient::ServiceClient(QByteArray updatePublicKey, std::unique_ptr<QNetworkAccessManager> transport, QObject* parent)
    : QObject(parent), manager_(std::move(transport)), updatePublicKey_(std::move(updatePublicKey)) {
    Q_ASSERT(manager_);
}
bool ServiceClient::allowedEndpoint(const QUrl& url) {
    return url.isValid() && url.scheme() == QStringLiteral("https") && url.host() == QStringLiteral("bluehalo.site") &&
        url.port(443) == 443 && url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment() &&
        (url.path() == QStringLiteral("/healthz") || url.path() == QStringLiteral("/api/installer/application/latest") ||
         url.path() == QStringLiteral("/api/installer/content"));
}
void ServiceClient::checkHealth() { request(Operation::Health); }
void ServiceClient::checkApplicationRelease() { request(Operation::Release); }
void ServiceClient::checkClientContent() { request(Operation::Content); }
void ServiceClient::cancel() {
    const auto snapshot = pending_;
    for (const auto& reply : snapshot) if (reply) reply->abort();
}
void ServiceClient::request(Operation operation) {
    const auto id = static_cast<int>(operation);
    if (pending_.value(id)) return;
    const QUrl url(operation == Operation::Health ? QStringLiteral("https://bluehalo.site/healthz") : operation == Operation::Release ?
                   QStringLiteral("https://bluehalo.site/api/installer/application/latest") : QStringLiteral("https://bluehalo.site/api/installer/content"));
    if (!allowedEndpoint(url)) return;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "RuTool/2.0.0-dev Windows");
    auto* reply = manager_->get(request); pending_.insert(id, reply);
    const qsizetype limit = operation == Operation::Health ? 4096 : operation == Operation::Content ? 384 * 1024 : 1024 * 1024;
    reply->setReadBufferSize(limit + 1);
    auto body = std::make_shared<QByteArray>();
    auto oversized = std::make_shared<bool>(false);
    auto* deadline = new QTimer(reply); deadline->setSingleShot(true); deadline->start(20000);
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::readyRead, this, [reply, body, oversized, limit] {
        body->append(reply->readAll());
        if (body->size() > limit) { *oversized = true; reply->abort(); }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, operation, body, oversized, limit] {
        pending_.remove(id);
        body->append(reply->readAll());
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool transport = reply->error() == QNetworkReply::NoError && !*oversized && body->size() <= limit;
        const auto diagnostic = QStringLiteral("HTTP: %1\nNetworkError: %2\nReceivedBytes: %3\nLimit: %4\n%5")
            .arg(status).arg(static_cast<int>(reply->error())).arg(body->size()).arg(limit).arg(reply->errorString());
        const auto report = [this, &diagnostic](const QString& message) {
            emit diagnosticError(QStringLiteral("HTTPS"), QStringLiteral("NETWORK"), message, diagnostic);
        };
        reply->deleteLater();
        if (operation == Operation::Health) {
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(*body, &error);
            const bool ok = transport && status == 200 && error.error == QJsonParseError::NoError &&
                document.isObject() && document.object().value(QStringLiteral("status")).toString() == QStringLiteral("ok");
            emit healthChecked(ok, ok ? QStringLiteral("API доступен по HTTPS") : QStringLiteral("Не удалось проверить API (HTTP %1)").arg(status));
            if (!ok) report(QStringLiteral("Не удалось проверить API"));
        } else if (operation == Operation::Content) {
            const auto content = parseClientContent(*body);
            const bool ok = transport && status == 200 && content.has_value();
            if (!ok) report(QStringLiteral("Не удалось обновить тексты и Credits"));
            emit contentChecked(ok, ok ? *body : QByteArray{}, ok ? QStringLiteral("Тексты и Credits обновлены") : QStringLiteral("Используется сохранённое содержимое"));
        } else {
            if (transport && status == 204) { emit releaseChecked(true, {}, QStringLiteral("Анонс обновления отсутствует")); return; }
            if (!transport || status != 200) { report(QStringLiteral("Не удалось получить анонс")); emit releaseChecked(false, {}, QStringLiteral("Не удалось получить анонс (HTTP %1)").arg(status)); return; }
            const auto release = verifyApplicationEnvelope(*body, updatePublicKey_);
            if (!release) report(release.error().message);
            emit releaseChecked(release.has_value(), release ? release->version : QString{},
                release ? QStringLiteral("Подпись подтверждена · %1 файлов").arg(release->files.size()) : release.error().message);
        }
    });
}
}
