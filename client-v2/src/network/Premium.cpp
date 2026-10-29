#include <rutool/network/Premium.hpp>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

namespace rutool {
namespace {
Error invalid() { return {ErrorCode::Network, QStringLiteral("Некорректные данные Premium на сервере.")}; }
Result<TranslationRelease> parseItem(const QJsonObject& object, bool main) {
    static const QRegularExpression id(QStringLiteral("^[a-f0-9]{32}$")), version(QStringLiteral("^[0-9]+\\.[0-9]+(?:\\.[0-9]+){0,2}$"));
    TranslationRelease item;
    item.id = apiValue(object, QStringLiteral("Id")).toString(); item.name = apiValue(object, QStringLiteral("Name")).toString();
    item.author = apiValue(object, QStringLiteral("Author")).toString(); item.version = apiValue(object, QStringLiteral("PackageVersion")).toString();
    item.sha256 = apiValue(object, QStringLiteral("PackageSha256")).toString(); item.originalHash = apiValue(object, QStringLiteral("OriginalDatabaseSha256")).toString();
    item.bytes = apiValue(object, QStringLiteral("PackageBytes")).toInteger(-1); item.url = QUrl(apiValue(object, QStringLiteral("PackageUrl")).toString());
    item.published = QDateTime::fromString(apiValue(object, QStringLiteral("CreatedAtUtc")).toString(), Qt::ISODateWithMs); item.premium = true; item.main = main;
    if ((main ? item.id != QStringLiteral("main") : !id.match(item.id).hasMatch()) || apiValue(object,QStringLiteral("Main")).toBool() != main ||
        item.name.trimmed().isEmpty() || item.name.size() > 100 || item.author.size() > 160 || !version.match(item.version).hasMatch() ||
        !isHash(item.sha256) || !isHash(item.originalHash) || item.bytes <= 0 || item.bytes > 2LL * 1024 * 1024 * 1024 || !item.published.isValid() ||
        !apiValue(object, QStringLiteral("Premium")).toBool() || !premiumAssetUrl(item.url) || !item.url.path().endsWith('/' + item.id)) return std::unexpected(invalid());
    return item;
}
}
bool premiumAssetUrl(const QUrl& url) {
    static const QRegularExpression path(QStringLiteral("^/api/installer/premium/packages/(?:[a-f0-9]{32}|main)$"));
    return allowedAssetUrl(url) && url.host() == QStringLiteral("bluehalo.site") && url.query().isEmpty() && path.match(url.path()).hasMatch();
}
Result<PremiumStatus> parsePremiumStatus(const QJsonObject& json) {
    const auto valid = apiValue(json, QStringLiteral("Valid"));
    const auto message = apiValue(json, QStringLiteral("Message")).toString();
    if (!valid.isBool() || message.size() > 256) return std::unexpected(invalid());
    PremiumStatus result{valid.toBool(), QDate::fromString(apiValue(json, QStringLiteral("ValidThrough")).toString(), Qt::ISODate), message};
    if (result.valid && !result.validThrough.isValid()) return std::unexpected(invalid());
    return result;
}
Result<std::vector<TranslationRelease>> parsePremiumCatalog(const QJsonObject& json) {
    const auto array = apiValue(json, QStringLiteral("Releases"));
    if (!array.isArray() || array.toArray().size() > 100) return std::unexpected(invalid());
    std::vector<TranslationRelease> result; QSet<QString> ids;
    for (const auto value : array.toArray()) {
        if (!value.isObject()) return std::unexpected(invalid());
        auto item = parseItem(value.toObject(), false); if (!item || ids.contains(item->id)) return std::unexpected(invalid());
        ids.insert(item->id); result.push_back(std::move(*item));
    }
    return result;
}
Result<PremiumCatalogInfo> parsePremiumCatalogResponse(const QJsonObject& json) {
    auto trials = parsePremiumCatalog(json); if (!trials) return std::unexpected(trials.error());
    PremiumCatalogInfo result{std::move(*trials), {}}; const auto main = apiValue(json, QStringLiteral("MainRelease"));
    if (!main.isUndefined() && !main.isNull()) {
        if (!main.isObject()) return std::unexpected(invalid());
        auto item = parseItem(main.toObject(), true); if (!item) return std::unexpected(item.error()); result.main = std::move(*item);
    }
    return result;
}
}
