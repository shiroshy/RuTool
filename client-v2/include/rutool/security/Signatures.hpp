#pragma once
#include <rutool/core/RutProtocol.hpp>
namespace rutool {
struct RutSignature {
    int formatVersion{};
    QString fileSha256, version, watermarkSha256, algorithm, signerId, signatureBase64;
};
Result<QByteArray> decodeBase64(const QString& text);
bool verifyRutSignature(const RutSignature& signature, const QByteArray& publicSpki);
bool verifySignedPayload(const QByteArray& payload, const QByteArray& p1363, const QByteArray& publicSpki);
}
