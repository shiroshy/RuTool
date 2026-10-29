#include "ClientController.hpp"
#include <rutool/platform/Crypto.hpp>
#include <QtConcurrent>

namespace { const QByteArray entropy("RuTool.Boosty.Code.v1"); }
void ClientController::initializePremiumConnections() {
    premiumTimer_.setSingleShot(true); premiumTimer_.setInterval(650);
    connect(&premiumTimer_, &QTimer::timeout, this, &ClientController::verifyBoostyCode);
    connect(&premiumWatcher_, &QFutureWatcherBase::finished, this, [this] {
        if (checkingBoostyCode_ != boostyCode_) { if (!boostyCode_.isEmpty()) verifyBoostyCode(); emit changed(); return; }
        const auto result = premiumWatcher_.result(); premiumAccepted_ = result && result->valid;
        premiumValidThrough_ = premiumAccepted_ ? result->validThrough : QDate{};
        premiumStatus_ = result ? result->message : QStringLiteral("Не удалось проверить код. Повторите ввод при доступном сервере.");
        if (result && !result->valid) operationalSettings_.remove(QStringLiteral("BoostyCodeProtected"));
        if (premiumAccepted_) {
            auto protectedCode = rutool::platform::protectUser(boostyCode_.toUtf8(), entropy);
            if (protectedCode) operationalSettings_[QStringLiteral("BoostyCodeProtected")] = QString::fromLatin1(protectedCode->toBase64());
            else premiumStatus_ += QStringLiteral(" · Код не сохранен на этом ПК");
        }
        saveOperations(); emit changed();
    });
    connect(&premiumCatalogWatcher_, &QFutureWatcherBase::finished, this, [this] {
        const auto result = premiumCatalogWatcher_.result(); premiumTranslations_.clear(); premiumReleases_.clear(); premiumMain_.reset();
        if (result) {
            premiumTranslations_ = result->releases; premiumMain_ = result->main;
            for (const auto& release : result->releases) premiumReleases_.append(QVariantMap{{QStringLiteral("id"), release.id}, {QStringLiteral("name"), release.name},
                {QStringLiteral("author"), release.author}, {QStringLiteral("publishedAt"), release.published.toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm"))}, {QStringLiteral("premium"), true}});
        }
        // Older servers may not offer Premium yet. Public downloads remain available.
        emit changed();
    });
}
void ClientController::setBoostyCode(const QString& code) {
    const auto value = code.trimmed().left(80);
    if (boostyCode_ == value) return;
    boostyCode_ = value; premiumAccepted_ = false; premiumValidThrough_ = {};
    operationalSettings_.remove(QStringLiteral("BoostyCodeProtected")); saveOperations();
    premiumTimer_.stop();
    premiumStatus_ = value.isEmpty() ? QStringLiteral("Код бусти открывает тестовые переводы Premium.") : QStringLiteral("Проверяем код бусти…");
    if (!value.isEmpty()) premiumTimer_.start();
    emit changed();
}
void ClientController::verifyBoostyCode() {
    if (premiumWatcher_.isRunning() || boostyCode_.isEmpty()) return;
    checkingBoostyCode_ = boostyCode_; const auto code = checkingBoostyCode_; const auto cancel = lifetime_;
    premiumWatcher_.setFuture(QtConcurrent::run([code, cancel]() -> rutool::Result<rutool::PremiumStatus> {
        rutool::InstallerApi api(cancel); auto result = api.post(QStringLiteral("api/installer/premium/check"), {{QStringLiteral("Code"), code}});
        if (!result) return std::unexpected(result.error());
        return rutool::parsePremiumStatus(*result);
    }));
    emit changed();
}
void ClientController::startPremium() {
    const auto protectedCode = QByteArray::fromBase64(operationalSettings_.value(QStringLiteral("BoostyCodeProtected")).toString().toLatin1());
    if (!protectedCode.isEmpty()) {
        auto code = rutool::platform::unprotectUser(protectedCode, entropy);
        if (code && code->bytes().size() <= 80) {
            boostyCode_ = QString::fromUtf8(code->bytes()); premiumStatus_ = QStringLiteral("Проверяем код бусти…"); premiumTimer_.start();
        }
    }
    loadPremiumCatalog();
}
void ClientController::loadPremiumCatalog() {
    if (premiumCatalogWatcher_.isRunning()) return;
    const auto cancel = lifetime_;
    premiumCatalogWatcher_.setFuture(QtConcurrent::run([cancel]() -> rutool::Result<rutool::PremiumCatalogInfo> {
        rutool::InstallerApi api(cancel); auto result = api.get(QStringLiteral("api/installer/premium/releases"));
        if (!result) return std::unexpected(result.error());
        return rutool::parsePremiumCatalogResponse(*result);
    }));
}
