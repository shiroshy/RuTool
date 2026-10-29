#include <rutool/network/Transfers.hpp>
#include <rutool/network/Premium.hpp>
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
#include <QStringDecoder>
#include <QElapsedTimer>

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
                              const QString& destination, const Cancellation& cancel, const TransferProgress& progress, QNetworkAccessManager* transport, const QByteArray& premiumCode) {
    const bool premium = premiumAssetUrl(initial);
    if ((!premiumCode.isEmpty() && (!premium || premiumCode.size() > 80 || premiumCode.contains('\r') || premiumCode.contains('\n'))) || (premium && premiumCode.isEmpty()))
        return std::unexpected(error(QStringLiteral("Для тестового перевода нужен действующий код бусти.")));
    if (expectedBytes < 0 || expectedBytes > 2LL * 1024 * 1024 * 1024 || !isHash(expectedHash) || !allowedAssetUrl(initial))
        return std::unexpected(error(QStringLiteral("Некорректные параметры загрузки.")));
    const auto partial = destination + QStringLiteral(".partial");
    if (QFileInfo::exists(destination)) {
        auto safe = platform::requireSafeFile(destination); auto existing = safe ? hashFile(destination, cancel) : Result<QString>{std::unexpected(safe.error())};
        if (existing && QFileInfo(destination).size() == expectedBytes && *existing == expectedHash) return destination;
        return std::unexpected(error(QStringLiteral("Существующая загрузка не прошла проверку целостности.")));
    }
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) return std::unexpected(error(QStringLiteral("Не удалось создать каталог загрузки.")));
    auto directorySafe = platform::requireSafeDirectory(QFileInfo(destination).absolutePath()); if (!directorySafe) return std::unexpected(directorySafe.error());
    if (QFileInfo::exists(partial)) {
        auto safe = platform::requireSafeFile(partial); if (!safe) return std::unexpected(safe.error());
        if (QFileInfo(partial).size() > expectedBytes && !QFile::remove(partial)) return std::unexpected(error(QStringLiteral("Не удалось очистить повреждённую загрузку.")));
    }
    auto space = platform::requireFreeSpace({{partial, static_cast<quint64>(expectedBytes - (QFileInfo::exists(partial) ? QFileInfo(partial).size() : 0))}});
    if (!space) return std::unexpected(space.error());
    QFile file(partial);
    if (!file.open(QIODevice::ReadWrite)) return std::unexpected(error(QStringLiteral("Не удалось открыть временный файл загрузки.")));
    bool preserve = true;
    struct Cleanup {
        QFile& file; QString partial; bool& preserve;
        Cleanup(QFile& output, QString path, bool& keep) : file(output), partial(std::move(path)), preserve(keep) {}
        ~Cleanup() { if (file.isOpen()) { file.flush(); file.close(); } if (!preserve) QFile::remove(partial); }
    } cleanup{file, partial, preserve};
    auto safe = platform::requireSafeFile(partial); if (!safe) return std::unexpected(safe.error());
    QNetworkAccessManager defaultTransport; auto& manager = transport ? *transport : defaultTransport; QUrl url = initial;
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (!allowedAssetUrl(url)) return std::unexpected(error(QStringLiteral("Недопустимое перенаправление загрузки.")));
        if (stopped(cancel)) return std::unexpected(Error{ErrorCode::Cancelled, QStringLiteral("Загрузка отменена.")});
        const qint64 offset = file.size(); QCryptographicHash hash(QCryptographicHash::Sha256); file.seek(0);
        while (!file.atEnd()) {
            if (stopped(cancel)) return std::unexpected(Error{ErrorCode::Cancelled, QStringLiteral("Загрузка отменена.")});
            const auto chunk = file.read(65536); if (chunk.isEmpty() && file.error() != QFileDevice::NoError) return std::unexpected(error(QStringLiteral("Не удалось прочитать незавершённую загрузку.")));
            hash.addData(chunk);
        }
        if (offset == expectedBytes && QString::fromLatin1(hash.result().toHex()) == expectedHash) {
            file.close(); auto flushed = platform::flushFile(partial); if (!flushed) return std::unexpected(flushed.error());
            if (!QFile::rename(partial, destination)) return std::unexpected(error(QStringLiteral("Не удалось сохранить проверенную загрузку.")));
            return destination;
        }
        QNetworkRequest request(url); request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy); request.setTransferTimeout(30000);
        if (premium) request.setRawHeader("Authorization", QByteArray("Bearer ") + premiumCode);
        if (offset) request.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(offset) + '-');
        auto* reply = manager.get(request); reply->setReadBufferSize(262144);
        QEventLoop loop; QTimer timeout, cancellation; QElapsedTimer progressClock; progressClock.start(); qint64 total = offset; bool invalid{}, redirect{}, timedOut{}, headersChecked{};
        const auto headers = [&] {
            if (headersChecked) return;
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status != 200 && status != 206) return;
            headersChecked = true;
            if (status == 200 && offset) { if (!file.resize(0) || !file.seek(0)) { invalid = true; reply->abort(); return; } total = 0; hash.reset(); }
            const auto length = expectedBytes - total;
            if ((status == 206 && reply->rawHeader("Content-Range") != QByteArray("bytes ") + QByteArray::number(offset) + '-' + QByteArray::number(expectedBytes - 1) + '/' + QByteArray::number(expectedBytes)) ||
                (reply->header(QNetworkRequest::ContentLengthHeader).isValid() && reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() != length)) { invalid = true; reply->abort(); }
        };
        const auto drain = [&] {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status >= 300 && status < 400) { redirect = true; reply->readAll(); return; }
            if (status != 200 && status != 206) { reply->readAll(); return; }
            headers(); if (invalid) return;
            while (reply->bytesAvailable()) {
                const auto chunk = reply->read(65536); total += chunk.size();
                if (total > expectedBytes || file.write(chunk) != chunk.size()) { invalid = true; reply->abort(); return; }
                hash.addData(chunk);
                if (progress && (total == expectedBytes || progressClock.elapsed() >= 100)) { progress(total, expectedBytes); progressClock.restart(); }
            }
        };
        QObject::connect(reply, &QIODevice::readyRead, &loop, drain);
        QObject::connect(reply, &QNetworkReply::metaDataChanged, &loop, headers);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        timeout.setSingleShot(true); QObject::connect(&timeout, &QTimer::timeout, &loop, [&] { timedOut = true; reply->abort(); });
        QObject::connect(&cancellation, &QTimer::timeout, &loop, [&] { if (stopped(cancel)) reply->abort(); });
        timeout.start(downloadDeadlineMilliseconds(expectedBytes)); cancellation.start(100); loop.exec(); drain();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); const auto networkError = reply->error();
        const auto location = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl(); delete reply;
        if (stopped(cancel)) return std::unexpected(Error{ErrorCode::Cancelled, QStringLiteral("Загрузка отменена.")});
        if (premium && status >= 300 && status < 400) return std::unexpected(error(QStringLiteral("Сервер вернул недопустимое перенаправление Premium-загрузки.")));
        if (premium && (status == 401 || status == 403)) return std::unexpected(Error{ErrorCode::AccessDenied, QStringLiteral("Premium-доступ отклонен. Проверьте срок действия кода бусти.")});
        if (redirect && (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) && !location.isEmpty()) { url = url.resolved(location); continue; }
        if (status == 416 && offset) { if (!file.resize(0) || !file.seek(0)) return std::unexpected(error(QStringLiteral("Не удалось перезапустить загрузку."))); continue; }
        if (timedOut) return std::unexpected(error(QStringLiteral("Истекло время загрузки. Незавершённый файл сохранён; повторите скачивание для продолжения.")));
        if (invalid) { preserve = false; return std::unexpected(error(QStringLiteral("Сервер вернул неверные границы или размер загрузки."))); }
        if (networkError != QNetworkReply::NoError || (status != 200 && status != 206)) return std::unexpected(error(QStringLiteral("Загрузка прервана (HTTP %1). Повторите скачивание для продолжения.").arg(status)));
        if (invalid || total != expectedBytes || QString::fromLatin1(hash.result().toHex()) != expectedHash) {
            preserve = false;
            return std::unexpected(error(QStringLiteral("Загрузка не завершена или не прошла проверку целостности (HTTP %1).").arg(status)));
        }
        if (!file.flush()) return std::unexpected(error(QStringLiteral("Ошибка записи загрузки.")));
        file.close(); auto flushed = platform::flushFile(partial); if (!flushed) return std::unexpected(flushed.error());
        if (!QFile::rename(partial, destination)) return std::unexpected(error(QStringLiteral("Не удалось сохранить проверенную загрузку.")));
        return destination;
    }
    return std::unexpected(error(QStringLiteral("Слишком много перенаправлений загрузки.")));
}
int downloadDeadlineMilliseconds(qint64 expectedBytes) {
    constexpr qint64 minimum = 600000, maximum = 12LL * 60 * 60 * 1000;
    return static_cast<int>(std::clamp(expectedBytes / 65536 * 1000 + 120000, minimum, maximum));
}
Result<void> validateReportAttachments(const QList<QUrl>& attachments) {
    if (attachments.size() > 10) return std::unexpected(error(QStringLiteral("Можно приложить не более 10 файлов.")));
    qint64 total{};
    for (const auto& url : attachments) {
        QFile file(url.toLocalFile()); const QFileInfo info(file);
        const auto invalid = [&info] { return std::unexpected(error(QStringLiteral("Вложение %1 недоступно или имеет неверный формат. Допустимы изображения и непустые UTF-8 TXT, всего до 8 МБ.").arg(info.fileName()))); };
        if (!url.isLocalFile() || !platform::requireSafeFile(info.absoluteFilePath()) || !file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 8 * 1024 * 1024 - total) return invalid();
        total += file.size(); const auto extension = info.suffix().toLower(); const auto header = file.peek(16);
        if (extension == QStringLiteral("txt")) {
            QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless); const QString text = decoder.decode(file.readAll());
            if (decoder.hasError() || text.isEmpty()) return invalid();
            for (const auto c : text) if (c.category() == QChar::Other_Control && c != u'\n' && c != u'\r' && c != u'\t') return invalid();
        } else {
            if (header.size() < 16) return invalid();
            const bool valid = (extension == QStringLiteral("png") && header.startsWith(QByteArray::fromHex("89504e470d0a1a0a"))) ||
                ((extension == QStringLiteral("jpg") || extension == QStringLiteral("jpeg")) && header.startsWith(QByteArray::fromHex("ffd8ff"))) ||
                (extension == QStringLiteral("webp") && header.startsWith("RIFF") && header.mid(8, 4) == "WEBP") ||
                (extension == QStringLiteral("gif") && (header.startsWith("GIF87a") || header.startsWith("GIF89a"))) ||
                (extension == QStringLiteral("bmp") && header.startsWith("BM")) ||
                ((extension == QStringLiteral("tif") || extension == QStringLiteral("tiff")) && (header.startsWith(QByteArray::fromHex("49492a00")) || header.startsWith(QByteArray::fromHex("4d4d002a"))));
            if (!valid) return invalid();
        }
    }
    return {};
}
Result<ReportResponse> sendReport(const ReportRequest& request, const Cancellation& cancel, QNetworkAccessManager* transport) {
    const QStringList topics{QStringLiteral("Общий вопрос"), QStringLiteral("Ошибки в переводе"), QStringLiteral("Ошибки в интерфейсе программы"), QStringLiteral("Техническая проблема"), QStringLiteral("Предложение об улучшении")};
    static const QRegularExpression uuidN(QStringLiteral("^[0-9a-fA-F]{32}$"));
    if (!uuidN.match(request.clientId).hasMatch() || request.text.trimmed().isEmpty() || request.text.toUcs4().size() > 400 || request.username.toUcs4().size() > 64 || !topics.contains(request.topic) || request.attachments.size() > 10)
        return std::unexpected(error(QStringLiteral("Проверьте тему, текст и контакт репорта.")));
    for (const auto& c : request.username) if (c.category() == QChar::Other_Control) return std::unexpected(error(QStringLiteral("Недопустимый символ в контакте.")));
    auto attachments = validateReportAttachments(request.attachments); if (!attachments) return std::unexpected(attachments.error());
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
