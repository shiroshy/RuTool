#include <rutool/releases/LinuxRelease.hpp>
#include <rutool/security/Signatures.hpp>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVersionNumber>
#include <vector>

namespace rutool {
namespace {
bool version(const QString& text, QVersionNumber& value) {
    static const QRegularExpression pattern(QStringLiteral("^(0|[1-9][0-9]*)[.](0|[1-9][0-9]*)[.](0|[1-9][0-9]*)$"));
    qsizetype suffix{};
    value = QVersionNumber::fromString(text, &suffix);
    return pattern.match(text).hasMatch() && suffix == text.size() && value.segmentCount() == 3;
}
}
bool validGithubRepository(const QString& repository) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9-]{0,38}/[A-Za-z0-9_-][A-Za-z0-9_.-]{0,99}$"));
    return pattern.match(repository).hasMatch();
}
Result<LinuxChannel> verifyLinuxChannel(const QByteArray& envelope, const QByteArray& publicKey,
    const QString& repository, const QString& currentVersion, qint64 minimumSequence) {
    const auto invalid = [] { return std::unexpected(Error{ErrorCode::InvalidInput, QStringLiteral("Некорректный или несовместимый Linux-канал обновлений.")}); };
    QVersionNumber current;
    if (envelope.size() > 1024 * 1024 || !validGithubRepository(repository) || !version(currentVersion, current)) return invalid();
    QJsonParseError error;
    const auto outer = QJsonDocument::fromJson(envelope, &error);
    if (error.error != QJsonParseError::NoError || !outer.isObject()) return invalid();
    auto payload = decodeBase64(outer.object().value(QStringLiteral("PayloadBase64")).toString());
    auto signature = decodeBase64(outer.object().value(QStringLiteral("SignatureBase64")).toString());
    if (!payload || !signature || !verifySignedPayload(*payload, *signature, publicKey))
        return std::unexpected(Error{ErrorCode::Signature, QStringLiteral("Подпись Linux-обновления не подтверждена.")});
    const auto document = QJsonDocument::fromJson(*payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return invalid();
    const auto object = document.object();
    LinuxChannel channel;
    channel.sequence = object.value(QStringLiteral("Sequence")).toInteger(-1);
    const auto routes = object.value(QStringLiteral("Routes"));
    if (object.value(QStringLiteral("FormatVersion")).toInteger(-1) != 1 ||
        object.value(QStringLiteral("Platform")).toString() != QStringLiteral("linux-x86_64") ||
        object.value(QStringLiteral("Repository")).toString() != repository ||
        channel.sequence <= 0 || channel.sequence > 9007199254740991LL || channel.sequence < minimumSequence ||
        !routes.isArray() || routes.toArray().size() > 64) return invalid();
    std::vector<std::pair<QVersionNumber, QVersionNumber>> intervals;
    static const QRegularExpression segment(QStringLiteral("^[A-Za-z0-9_-][A-Za-z0-9_.-]{0,127}$"));
    for (const auto& value : routes.toArray()) {
        if (!value.isObject()) return invalid();
        const auto route = value.toObject();
        QVersionNumber from, before, next;
        LinuxRelease release;
        release.version = route.value(QStringLiteral("Version")).toString();
        release.bytes = route.value(QStringLiteral("Bytes")).toInteger(-1);
        release.sha256 = route.value(QStringLiteral("Sha256")).toString();
        const auto tag = route.value(QStringLiteral("Tag")).toString();
        const auto asset = route.value(QStringLiteral("Asset")).toString();
        if (!version(route.value(QStringLiteral("FromVersion")).toString(), from) ||
            !version(route.value(QStringLiteral("BeforeVersion")).toString(), before) || !version(release.version, next) ||
            from >= before || next < before || release.bytes <= 0 || release.bytes > 2LL * 1024 * 1024 * 1024 ||
            !isHash(release.sha256) || !segment.match(tag).hasMatch() || !segment.match(asset).hasMatch() ||
            !asset.endsWith(QStringLiteral(".AppImage"))) return invalid();
        for (const auto& interval : intervals)
            if (from < interval.second && interval.first < before) return invalid();
        intervals.emplace_back(from, before);
        release.url = QUrl(QStringLiteral("https://github.com/%1/releases/download/%2/%3").arg(repository, tag, asset));
        if (current >= from && current < before) channel.next = std::move(release);
    }
    return channel;
}
}
