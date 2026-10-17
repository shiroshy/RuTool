#include <rutool/network/ClientContent.hpp>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
namespace rutool {
Result<ClientContent> parseClientContent(const QByteArray& json) {
    const auto invalid = [] { return std::unexpected(Error{ErrorCode::InvalidInput, QStringLiteral("Некорректный FAQ или пользовательское соглашение")}); };
    if (json.size() > 384 * 1024) return invalid();
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return invalid();
    const auto object = doc.object();
    ClientContent content{object.value(QStringLiteral("revision")).toInteger(-1), object.value(QStringLiteral("faqText")).toString(),
        object.value(QStringLiteral("licenseText")).toString(), object.value(QStringLiteral("licenseSha256")).toString()};
    if (object.value(QStringLiteral("formatVersion")).toInteger() != 1 || content.revision <= 0 ||
        content.faq.size() > 32000 || content.license.trimmed().isEmpty() || content.license.size() > 64000) return invalid();
    for (const auto& text : {content.faq, content.license}) for (const auto c : text)
        if (c.category() == QChar::Other_Control && c != u'\r' && c != u'\n' && c != u'\t') return invalid();
    const auto hash = QString::fromLatin1(QCryptographicHash::hash(content.license.toUtf8(), QCryptographicHash::Sha256).toHex());
    if (content.licenseHash != hash) return invalid();
    return content;
}
}
