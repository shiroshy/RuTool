#include <rutool/network/ClientContent.hpp>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
namespace rutool {
QVariantList defaultCredits() {
    QVariantList entries;
    for (const auto& pair : {std::pair{QStringLiteral("Кодер, автор, редактор"), QStringLiteral("Shizuka")},
         std::pair{QStringLiteral("Совладелец"), QStringLiteral("Ui")},
         std::pair{QStringLiteral("Редактор"), QStringLiteral("AfteraDev")},
         std::pair{QStringLiteral("Редактор"), QStringLiteral("Kostya")},
         std::pair{QStringLiteral("Редактор"), QStringLiteral("Magnisimus")}})
        entries.append(QVariantMap{{QStringLiteral("role"), pair.first}, {QStringLiteral("name"), pair.second}});
    return entries;
}
Result<ClientContent> parseClientContent(const QByteArray& json) {
    const auto invalid = [] { return std::unexpected(Error{ErrorCode::InvalidInput, QStringLiteral("Некорректное содержимое RuTool")}); };
    if (json.size() > 384 * 1024) return invalid();
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return invalid();
    const auto object = doc.object();
    ClientContent content{object.value(QStringLiteral("revision")).toInteger(-1), object.value(QStringLiteral("faqText")).toString(),
        object.value(QStringLiteral("licenseText")).toString(), object.value(QStringLiteral("licenseSha256")).toString(), defaultCredits()};
    if (object.value(QStringLiteral("formatVersion")).toInteger() != 1 || content.revision <= 0 ||
        content.faq.size() > 32000 || content.license.trimmed().isEmpty() || content.license.size() > 64000) return invalid();
    for (const auto& text : {content.faq, content.license}) for (const auto c : text)
        if (c.category() == QChar::Other_Control && c != u'\r' && c != u'\n' && c != u'\t') return invalid();
    const auto hash = QString::fromLatin1(QCryptographicHash::hash(content.license.toUtf8(), QCryptographicHash::Sha256).toHex());
    if (content.licenseHash != hash) return invalid();
    if (object.contains(QStringLiteral("credits"))) {
        const auto value = object.value(QStringLiteral("credits"));
        if (!value.isArray() || value.toArray().size() > 100) return invalid();
        content.credits.clear();
        for (const auto& entry : value.toArray()) {
            if (!entry.isObject()) return invalid();
            const auto credit = entry.toObject();
            const auto role = credit.value(QStringLiteral("role"));
            const auto name = credit.value(QStringLiteral("name"));
            if (!role.isString() || !name.isString() || role.toString().trimmed().isEmpty() || name.toString().trimmed().isEmpty() ||
                role.toString().size() > 120 || name.toString().size() > 160) return invalid();
            for (const auto& text : {role.toString(), name.toString()}) for (const auto c : text)
                if (c.category() == QChar::Other_Control) return invalid();
            content.credits.append(QVariantMap{{QStringLiteral("role"), role.toString()}, {QStringLiteral("name"), name.toString()}});
        }
    }
    return content;
}
}
