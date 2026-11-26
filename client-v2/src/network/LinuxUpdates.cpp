#include <rutool/network/LinuxUpdates.hpp>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QThread>
#include <QCoreApplication>
#include <QTimer>
#include <algorithm>

namespace rutool {
Result<QByteArray> fetchLinuxChannel(const QString& repository, const QString& channelRef,
    const Cancellation& cancel, QNetworkAccessManager* transport) {
    const auto failure = [] { return std::unexpected(Error{ErrorCode::Network, QStringLiteral("Не удалось получить Linux-канал обновлений с GitHub.")}); };
    if (cancel && cancel->load()) return std::unexpected(Error{ErrorCode::Cancelled, QStringLiteral("Проверка обновления отменена.")});
    static const QRegularExpression tag(QStringLiteral("^[A-Za-z0-9_-][A-Za-z0-9_.-]{0,127}$"));
    if (!validGithubRepository(repository) || !tag.match(channelRef).hasMatch() ||
        QThread::currentThread() == QCoreApplication::instance()->thread()) return failure();
    QNetworkAccessManager fallback;
    auto& manager = transport ? *transport : fallback;
    QUrl url(QStringLiteral("https://raw.githubusercontent.com/%1/%2/linux-channel-v1.json").arg(repository, channelRef));
    QElapsedTimer elapsed; elapsed.start();
    for (int redirects = 0; redirects < 5; ++redirects) {
        if (cancel && cancel->load()) return std::unexpected(Error{ErrorCode::Cancelled, QStringLiteral("Проверка обновления отменена.")});
        if (elapsed.elapsed() >= 30000 || !url.isValid() || url.scheme() != QStringLiteral("https") || url.port(443) != 443 ||
            !url.userInfo().isEmpty() || url.hasFragment() ||
            !QStringList{QStringLiteral("raw.githubusercontent.com"), QStringLiteral("github.com"), QStringLiteral("release-assets.githubusercontent.com"), QStringLiteral("objects.githubusercontent.com")}.contains(url.host())) return failure();
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(15000);
        auto* reply = manager.get(request); reply->setReadBufferSize(65536);
        QByteArray body; bool oversized{};
        const auto drain = [&] {
            body += reply->read(1024 * 1024 + 1 - body.size());
            if (body.size() > 1024 * 1024) { oversized = true; reply->abort(); }
        };
        QEventLoop loop; QTimer timeout, cancellation;
        timeout.setSingleShot(true);
        QObject::connect(reply, &QIODevice::readyRead, &loop, drain);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timeout, &QTimer::timeout, &loop, [reply] { reply->abort(); });
        QObject::connect(&cancellation, &QTimer::timeout, &loop, [&] { if (cancel && cancel->load()) reply->abort(); });
        timeout.start(static_cast<int>(std::max<qint64>(1, 30000 - elapsed.elapsed()))); cancellation.start(50);
        loop.exec(); drain();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto location = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        const auto error = reply->error(); delete reply;
        if (cancel && cancel->load()) return std::unexpected(Error{ErrorCode::Cancelled, QStringLiteral("Проверка обновления отменена.")});
        if (oversized) return failure();
        if (status >= 300 && status < 400 && !location.isEmpty()) { url = url.resolved(location); continue; }
        if (status == 200 && error == QNetworkReply::NoError) return body;
        return failure();
    }
    return failure();
}
}
