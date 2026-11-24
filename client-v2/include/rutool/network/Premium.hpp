#pragma once
#include <rutool/network/Transfers.hpp>
#include <QDate>
#include <optional>

namespace rutool {
struct PremiumStatus { bool valid{}; QDate validThrough; QString message; };
struct PremiumAddon { qint64 bytes{}; QString sha256; };
struct PremiumCatalogInfo {
    std::vector<TranslationRelease> releases;
    std::optional<TranslationRelease> main;
    std::optional<PremiumAddon> loadingScreens{};
};
Result<PremiumStatus> parsePremiumStatus(const QJsonObject& json);
Result<std::vector<TranslationRelease>> parsePremiumCatalog(const QJsonObject& json);
Result<PremiumCatalogInfo> parsePremiumCatalogResponse(const QJsonObject& json);
bool premiumAssetUrl(const QUrl& url);
}
