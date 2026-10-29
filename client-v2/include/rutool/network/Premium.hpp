#pragma once
#include <rutool/network/Transfers.hpp>
#include <QDate>
#include <optional>

namespace rutool {
struct PremiumStatus { bool valid{}; QDate validThrough; QString message; };
struct PremiumCatalogInfo { std::vector<TranslationRelease> releases; std::optional<TranslationRelease> main; };
Result<PremiumStatus> parsePremiumStatus(const QJsonObject& json);
Result<std::vector<TranslationRelease>> parsePremiumCatalog(const QJsonObject& json);
Result<PremiumCatalogInfo> parsePremiumCatalogResponse(const QJsonObject& json);
bool premiumAssetUrl(const QUrl& url);
}
