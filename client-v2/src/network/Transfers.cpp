#include <rutool/network/Transfers.hpp>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QTimer>
#include <QUuid>
#include <QVersionNumber>
#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <rutool/platform/FileOps.hpp>
#include <algorithm>

namespace rutool {
namespace {
Error error(const QString& text) { return {ErrorCode::Network, text}; }
bool validVersion(const QString& version, QVersionNumber& parsed) {
    qsizetype suffix{}; parsed = QVersionNumber::fromString(version, &suffix); return suffix == version.size() && parsed.segmentCount() >= 2 && parsed.segmentCount() <= 4;
}
bool stopped(const Cancellation& cancel) { return cancel && cancel->load(); }
}
Result<TranslationRelease> parseTranslationRelease(const QJsonObject& json, qint64 minimumSequence) {
    TranslationRelease result;
    result.id = apiValue(json, QStringLiteral("ReleaseId")).toString(); result.version = apiValue(json, QStringLiteral("PackageVersion")).toString();
    result.originalHash = apiValue(json, QStringLiteral("OriginalDatabaseSha256")).toString(); result.sha256 = apiValue(json, QStringLiteral("PackageSha256")).toString();
    result.bytes = apiValue(json, QStringLiteral("PackageBytes")).toInteger(-1); result.sequence = apiValue(json, QStringLiteral("Sequence")).toInteger(-1);
    result.url = QUrl(apiValue(json, QStringLiteral("PackageUrl")).toString()); result.published = QDateTime::fromString(apiValue(json, QStringLiteral("PublishedAtUtc")).toString(), Qt::ISODateWithMs);
    result.attestation = apiValue(json, QStringLiteral("PackageAttestation")).toObject(); result.author = apiValue(result.attestation, QStringLiteral("SignerId")).toString();
    QVersionNumber package, minimum;
    const bool github = result.url.host() == QStringLiteral("github.com");
    if (apiValue(json, QStringLiteral("FormatVersion")).toInt() != 1 || result.id.trimmed().isEmpty() || result.id.size() > 128 ||
        !validVersion(result.version, package) || !validVersion(apiValue(json, QStringLiteral("MinimumInstallerVersion")).toString(), minimum) || minimum > QVersionNumber::fromString(QStringLiteral(RUTOOL_APP_VERSION)) ||
        result.bytes <= 0 || result.bytes > 2LL * 1024 * 1024 * 1024 || !isHash(result.sha256) || !isHash(result.originalHash) || !result.published.isValid() ||
        !allowedAssetUrl(result.url) || result.url.hasQuery() || !result.url.fragment().isEmpty() || result.sequence < minimumSequence ||
        (github ? result.sequence <= 0 || result.attestation.isEmpty() || result.url.path() != QStringLiteral("/shiroshy/rut_files/releases/download/%1/translation.rut").arg(result.id) ||
            apiValue(result.attestation, QStringLiteral("FileSha256")).toString() != result.sha256 || apiValue(result.attestation, QStringLiteral("WatermarkSha256")).toString() != result.originalHash
            : result.url.host() != QStringLiteral("bluehalo.site") || !result.url.path().startsWith(QStringLiteral("/installer/packages/")) || !result.url.path().endsWith(QStringLiteral(".rut"), Qt::CaseInsensitive)))
        return std::unexpected(error(QStringLiteral("Некорректный, устаревший или несовместимый каталог переводов.")));
    return result;
}
bool allowedAssetUrl(const QUrl& url) {
    return url.isValid() && url.scheme() == QStringLiteral("https") && url.port(443) == 443 && url.userInfo().isEmpty() && url.fragment().isEmpty() &&
        QStringList{QStringLiteral("bluehalo.site"), QStringLiteral("github.com"), QStringLiteral("release-assets.githubusercontent.com"), QStringLiteral("objects.githubusercontent.com")}.contains(url.host());
}
Result<QString> downloadAsset(const QUrl& initial, qint64 expectedBytes, const QString& expectedHash,
                              const QString& destination, const Cancellation& cancel, const TransferProgress& progress) {
    if (expectedBytes < 0 || expectedBytes >= 2LL * 1024 * 1024 * 1024 || !isHash(expectedHash) || !allowedAssetUrl(initial))
        return std::unexpected(error(QStringLiteral("Некорректные параметры загрузки.")));
    const auto partial = destination + QStringLiteral(".partial");
    if (QFileInfo::exists(destination) || QFileInfo::exists(partial)) return std::unexpected(error(QStringLiteral("Файл загрузки уже существует.")));
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) return std::unexpected(error(QStringLiteral("Не удалось создать каталог загрузки.")));
    QFile file(partial);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return std::unexpected(error(QStringLiteral("Не удалось создать временный файл загрузки.")));
    struct Cleanup {
        QFile& file; QString partial;
        Cleanup(QFile& output, QString path) : file(output), partial(std::move(path)) {}
        ~Cleanup() { file.close(); QFile::remove(partial); }
    } cleanup{file, partial};
    auto safe = platform::requireSafeFile(partial); if (!safe) return std::unexpected(safe.error());
    QNetworkAccessManager manager; QUrl url = initial;
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (!allowedAssetUrl(url)) return std::unexpected(error(QStringLiteral("Недопустимое перенаправление загрузки.")));
        if (stopped(cancel)) return std::unexpected(Error{ErrorCode::Cancelled, QStringLiteral("Загрузка отменена.")});
        QNetworkRequest request(url); request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy); request.setTransferTimeout(30000);
        auto* reply = manager.get(request); reply->setReadBufferSize(262144);
        QEventLoop loop; QTimer timeout, cancellation; QCryptographicHash hash(QCryptographicHash::Sha256); qint64 total{}; bool invalid{}, redirect{}, timedOut{};
        const auto drain = [&] {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status >= 300 && status < 400) { redirect = true; reply->readAll(); return; }
            if (status != 200) { reply->readAll(); return; }
            while (reply->bytesAvailable()) {
                const auto chunk = reply->read(65536); total += chunk.size();
                if (total > expectedBytes || file.write(chunk) != chunk.size()) { invalid = true; reply->abort(); return; }
                hash.addData(chunk); if (progress) progress(total, expectedBytes);
            }
        };
        QObject::connect(reply, &QIODevice::readyRead, &loop, drain);
        QObject::connect(reply, &QNetworkReply::metaDataChanged, &loop, [&] {
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 && reply->header(QNetworkRequest::ContentLengthHeader).isValid() && reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() != expectedBytes) { invalid = true; reply->abort(); }
        });
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        timeout.setSingleShot(true); QObject::connect(&timeout, &QTimer::timeout, &loop, [&] { timedOut = true; reply->abort(); });
        QObject::connect(&cancellation, &QTimer::timeout, &loop, [&] { if (stopped(cancel)) reply->abort(); });
        timeout.start(600000); cancellation.start(100); loop.exec(); drain();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); const auto networkError = reply->error();
        const auto location = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl(); delete reply;
        if (stopped(cancel)) return std::unexpected(Error{ErrorCode::Cancelled, QStringLiteral("Загрузка отменена.")});
        if (redirect && (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) && !location.isEmpty()) { url = url.resolved(location); continue; }
        if (status != 200 || networkError != QNetworkReply::NoError || invalid || timedOut || total != expectedBytes || QString::fromLatin1(hash.result().toHex()) != expectedHash)
            return std::unexpected(error(QStringLiteral("Загрузка не завершена или не прошла проверку целостности (HTTP %1).").arg(status)));
        if (!file.flush()) return std::unexpected(error(QStringLiteral("Ошибка записи загрузки.")));
        file.close(); auto flushed = platform::flushFile(partial); if (!flushed) return std::unexpected(flushed.error());
        if (!QFile::rename(partial, destination)) return std::unexpected(error(QStringLiteral("Не удалось сохранить проверенную загрузку.")));
        return destination;
    }
    return std::unexpected(error(QStringLiteral("Слишком много перенаправлений загрузки.")));
}
Result<ReportResponse> sendReport(const ReportRequest& request, const Cancellation& cancel, QNetworkAccessManager* transport) {
    const QStringList topics{QStringLiteral("Общий вопрос"), QStringLiteral("Ошибки в переводе"), QStringLiteral("Ошибки в интерфейсе программы"), QStringLiteral("Техническая проблема"), QStringLiteral("Предложение об улучшении")};
    static const QRegularExpression uuidN(QStringLiteral("^[0-9a-fA-F]{32}$"));
    if (!uuidN.match(request.clientId).hasMatch() || request.text.trimmed().isEmpty() || request.text.toUcs4().size() > 400 || request.username.toUcs4().size() > 64 || !topics.contains(request.topic) || request.attachments.size() > 10)
        return std::unexpected(error(QStringLiteral("Проверьте тему, текст и контакт репорта.")));
    for (const auto& c : request.username) if (c.category() == QChar::Other_Control) return std::unexpected(error(QStringLiteral("Недопустимый символ в контакте.")));
    QNetworkAccessManager defaultTransport; auto& manager = transport ? *transport : defaultTransport; QHttpMultiPart form(QHttpMultiPart::FormDataType);
    const auto field = [&form](const QByteArray& name, const QString& value) { QHttpPart part; part.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"%1\"").arg(QString::fromLatin1(name))); part.setBody(value.toUtf8()); form.append(part); };
    field("clientId", request.clientId); field("username", request.username); field("text", request.text); field("topic", request.topic);
    qint64 total{}; std::vector<std::unique_ptr<QFile>> files;
    const QMap<QString, QString> types{{QStringLiteral("png"), QStringLiteral("image/png")}, {QStringLiteral("jpg"), QStringLiteral("image/jpeg")}, {QStringLiteral("jpeg"), QStringLiteral("image/jpeg")},
        {QStringLiteral("webp"), QStringLiteral("image/webp")}, {QStringLiteral("gif"), QStringLiteral("image/gif")}, {QStringLiteral("bmp"), QStringLiteral("image/bmp")},
        {QStringLiteral("tif"), QStringLiteral("image/tiff")}, {QStringLiteral("tiff"), QStringLiteral("image/tiff")}, {QStringLiteral("txt"), QStringLiteral("text/plain; charset=utf-8")}};
    for (const auto& url : request.attachments) {
        auto file = std::make_unique<QFile>(url.toLocalFile()); const QFileInfo info(*file); auto safe = platform::requireSafeFile(info.absoluteFilePath());
        if (!url.isLocalFile() || !safe || !types.contains(info.suffix().toLower()) || !file->open(QIODevice::ReadOnly) || file->size() <= 0 || file->size() > 8 * 1024 * 1024 - total)
            return std::unexpected(error(QStringLiteral("Вложения недоступны или превышают лимит 8 МБ.")));
        total += file->size(); QHttpPart part; auto name = info.fileName(); name.replace(QLatin1Char('"'), QLatin1Char('_')); name.replace(QLatin1Char('\r'), QLatin1Char('_')); name.replace(QLatin1Char('\n'), QLatin1Char('_'));
        part.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"files\"; filename=\"%1\"").arg(name));
        part.setHeader(QNetworkRequest::ContentTypeHeader, types.value(info.suffix().toLower())); part.setBodyDevice(file.get()); form.append(part); files.push_back(std::move(file));
    }
    QNetworkRequest endpoint(QUrl(QStringLiteral("https://bluehalo.site/api/installer/report"))); endpoint.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy); endpoint.setTransferTimeout(30000);
    auto* reply = manager.post(endpoint, &form); QEventLoop loop; QTimer timeout, cancellation; QByteArray bytes; bool oversized{};
    QObject::connect(reply, &QIODevice::readyRead, &loop, [&] { bytes += reply->read(65537 - bytes.size()); if (bytes.size() > 65536) { oversized = true; reply->abort(); } });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.setSingleShot(true); QObject::connect(&timeout, &QTimer::timeout, &loop, [reply] { reply->abort(); });
    QObject::connect(&cancellation, &QTimer::timeout, &loop, [&] { if (stopped(cancel)) reply->abort(); }); timeout.start(120000); cancellation.start(100); loop.exec();
    bytes += reply->read(65537 - bytes.size()); const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); const auto response = QJsonDocument::fromJson(bytes).object(); const auto code = reply->error(); delete reply;
    if (stopped(cancel)) return std::unexpected(Error{ErrorCode::Cancelled, QStringLiteral("Отправка отменена.")});
    const auto next = QDateTime::fromString(response.value(QStringLiteral("nextAllowedAtUtc")).toString(), Qt::ISODateWithMs);
    if (status == 429) return ReportResponse{QDateTime::currentDateTimeUtc().addSecs(std::clamp(response.value(QStringLiteral("retryAfterSeconds")).toInt(), 1, 3600)), false, response.value(QStringLiteral("message")).toString(QStringLiteral("Подождите перед следующим репортом."))};
    if (code != QNetworkReply::NoError || status < 200 || status >= 300 || oversized || !next.isValid()) return std::unexpected(error(QStringLiteral("Сервер не принял репорт (HTTP %1).").arg(status)));
    return ReportResponse{next, true, QStringLiteral("Репорт отправлен. Спасибо!")};
}
}
