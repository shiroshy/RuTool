#include <rutool/core/RutProtocol.hpp>
#include <QCryptographicHash>
#include <QtEndian>
#include <algorithm>

namespace rutool {
namespace {
QByteArray dotNetUtf8(const QString& text) {
    QString normalized;
    normalized.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const auto c = text.at(i);
        if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            normalized.append(c); normalized.append(text.at(++i));
        } else {
            normalized.append(c.isSurrogate() ? QChar(0xfffd) : c);
        }
    }
    return normalized.toUtf8();
}
}
bool isHash(const QString& value) {
    return value.size() == 64 && std::ranges::all_of(value, [](QChar c) {
        const auto u = c.unicode();
        return (u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F');
    });
}
Result<QByteArray> signatureMessage(const QString& fileHash, const QString& version,
                                   const QString& watermarkHash, const QString& signer) {
    if (!isHash(fileHash) || !isHash(watermarkHash) || version.trimmed().isEmpty() ||
        version.size() > 32 || signer.trimmed().isEmpty() || signer.size() > 100 ||
        version.contains(u'\n') || signer.contains(u'\n'))
        return std::unexpected(Error{ErrorCode::InvalidInput, QStringLiteral("Некорректные данные подписи .rut")});
    return dotNetUtf8(QStringLiteral("RuTool.Signature.v1\n") + version + u'\n' + fileHash.toLower() +
            u'\n' + watermarkHash.toLower() + u'\n' + signer + u'\n');
}
Result<QByteArray> bundleMessage(int formatVersion, const std::array<QString, 5>& hashes) {
    if (formatVersion != 2 || !std::ranges::all_of(hashes, isHash))
        return std::unexpected(Error{ErrorCode::InvalidInput, QStringLiteral("Некорректные хеши bundle v2")});
    QByteArray message("RuTool.bundle.v2\n");
    for (const auto& hash : hashes) { message += hash.toUtf8(); message += '\n'; }
    return message;
}
Result<QString> bundleHash(int formatVersion, const std::array<QString, 5>& hashes) {
    auto message = bundleMessage(formatVersion, hashes);
    if (!message) return std::unexpected(message.error());
    return QString::fromLatin1(QCryptographicHash::hash(*message, QCryptographicHash::Sha256).toHex());
}
QByteArray chunkNonce(const QByteArray& prefix, quint32 index) {
    if (prefix.size() != 8) return {};
    QByteArray value = prefix; value.resize(12);
    qToLittleEndian(index, value.data() + 8);
    return value;
}
QByteArray chunkAad(quint32 index) {
    return chunkNonce(QByteArray("RUT111\0\0", 8), index);
}
}
