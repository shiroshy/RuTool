#include <rutool/security/Signatures.hpp>
#include <rutool/platform/SignatureVerifier.hpp>
namespace rutool {
Result<QByteArray> decodeBase64(const QString& text) {
    QByteArray ascii;
    for (QChar c : text) {
        const auto u = c.unicode();
        if (u == ' ' || u == '\t' || u == '\r' || u == '\n') continue;
        if (u > 127) return std::unexpected(Error{ErrorCode::InvalidInput, QStringLiteral("Некорректный Base64")});
        ascii += static_cast<char>(u);
    }
    const auto decoded = QByteArray::fromBase64Encoding(ascii, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || ascii.size() % 4 != 0)
        return std::unexpected(Error{ErrorCode::InvalidInput, QStringLiteral("Некорректный Base64")});
    return decoded.decoded;
}
bool verifySignedPayload(const QByteArray& payload, const QByteArray& p1363, const QByteArray& publicSpki) {
    return platform::verifyP256(payload, p1363, publicSpki);
}
bool verifyRutSignature(const RutSignature& signature, const QByteArray& publicSpki) {
    if (signature.formatVersion != 1 || signature.algorithm != QStringLiteral("ECDSA-P256-SHA256")) return false;
    auto message = signatureMessage(signature.fileSha256, signature.version, signature.watermarkSha256, signature.signerId);
    auto bytes = decodeBase64(signature.signatureBase64);
    return message && bytes && verifySignedPayload(*message, *bytes, publicSpki);
}
}
