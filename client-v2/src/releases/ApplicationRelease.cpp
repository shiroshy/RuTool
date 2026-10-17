#include <rutool/releases/ApplicationRelease.hpp>
#include <rutool/security/Signatures.hpp>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QStringList>

namespace rutool {
bool allowedApplicationPath(const QString& path) {
    if (path.isEmpty() || path.size() > 220 || path.contains(u'\\') || path.contains(u':') || path.startsWith(u'/')) return false;
    for (QChar c : path) if (c.category() == QChar::Other_Control) return false;
    if (path != QStringLiteral("RuToolInstaller.exe") && path != QStringLiteral("README.md") &&
        !path.startsWith(QStringLiteral("app/")) && !path.startsWith(QStringLiteral("updater/"))) return false;
    const QStringList reserved{QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"), QStringLiteral("NUL"), QStringLiteral("CLOCK$")};
    for (const auto& part : path.split(u'/')) {
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral("..") || part.endsWith(u'.') || part.endsWith(u' ')) return false;
        for (QChar c : part) if (QStringLiteral("<>\"|?*").contains(c)) return false;
        const auto stem = part.section(u'.', 0, 0).toUpper();
        if (reserved.contains(stem) || (stem.size() == 4 &&
            (stem.startsWith(QStringLiteral("COM")) || stem.startsWith(QStringLiteral("LPT"))) && stem.at(3).isDigit())) return false;
    }
    return true;
}
Result<ApplicationRelease> verifyApplicationEnvelope(const QByteArray& envelope, const QByteArray& pinnedSpki) {
    const auto invalid = [] { return std::unexpected(Error{ErrorCode::InvalidInput, QStringLiteral("Некорректный манифест обновления")}); };
    if (envelope.size() > 1024 * 1024) return invalid();
    QJsonParseError error;
    const auto outer = QJsonDocument::fromJson(envelope, &error);
    if (error.error != QJsonParseError::NoError || !outer.isObject()) return invalid();
    auto payload = decodeBase64(outer.object().value(QStringLiteral("PayloadBase64")).toString());
    auto signature = decodeBase64(outer.object().value(QStringLiteral("SignatureBase64")).toString());
    if (!payload || payload->isEmpty() || !signature || !verifySignedPayload(*payload, *signature, pinnedSpki))
        return std::unexpected(Error{ErrorCode::Signature, QStringLiteral("Подпись обновления не подтверждена")});
    const auto document = QJsonDocument::fromJson(*payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return invalid();
    const auto object = document.object();
    ApplicationRelease release;
    release.sequence = object.value(QStringLiteral("Sequence")).toInteger(-1);
    release.version = object.value(QStringLiteral("Version")).toString();
    release.repository = object.value(QStringLiteral("Repository")).toString();
    release.tag = object.value(QStringLiteral("Tag")).toString();
    if (object.value(QStringLiteral("FormatVersion")).toInteger(-1) != 1 || release.sequence <= 0 ||
        release.repository != QStringLiteral("shiroshy/RuTool")) return invalid();
    const auto parts = release.version.split(u'.');
    if (parts.size() < 2 || parts.size() > 4) return invalid();
    QStringList normalized;
    for (const auto& part : parts) {
        bool ok{}; const int number = part.toInt(&ok);
        if (!ok || number < 0) return invalid();
        normalized.append(QString::number(number));
    }
    if (release.tag != u'v' + normalized.join(u'.')) return invalid();
    const auto files = object.value(QStringLiteral("Files"));
    if (!files.isArray() || files.toArray().isEmpty() || files.toArray().size() > 900) return invalid();
    QSet<QString> names; qint64 total{}; bool hasExe{}, hasDll{};
    for (const auto& value : files.toArray()) {
        if (!value.isObject()) return invalid();
        const auto file = value.toObject();
        ApplicationFile entry{file.value(QStringLiteral("Path")).toString(), file.value(QStringLiteral("Bytes")).toInteger(-1),
            file.value(QStringLiteral("Sha256")).toString(), file.value(QStringLiteral("Asset")).toString()};
        if (!allowedApplicationPath(entry.path) || names.contains(entry.path.toCaseFolded()) ||
            entry.bytes < 0 || entry.bytes >= 2LL * 1024 * 1024 * 1024 || !isHash(entry.sha256) || entry.asset != entry.sha256 + QStringLiteral(".bin")) return invalid();
        total += entry.bytes;
        if (total > 4LL * 1024 * 1024 * 1024) return invalid();
        names.insert(entry.path.toCaseFolded());
        hasExe |= entry.path == QStringLiteral("RuToolInstaller.exe");
        hasDll |= entry.path == QStringLiteral("app/RuToolInstaller.dll");
        release.files.append(std::move(entry));
    }
    if (!hasExe || !hasDll) return invalid();
    return release;
}
}
