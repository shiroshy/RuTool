#pragma once
#include <QByteArray>
#include <QString>
#include <array>
#include <expected>

namespace rutool {
enum class ErrorCode { InvalidInput, UnsupportedFormat, Truncated, Io, Cancelled, Signature, Network };
struct Error { ErrorCode code{ErrorCode::InvalidInput}; QString message; QString details{}; };
template<class T> using Result = std::expected<T, Error>;
bool isHash(const QString& value);
Result<QByteArray> signatureMessage(const QString& fileHash, const QString& version,
                                   const QString& watermarkHash, const QString& signer);
Result<QByteArray> bundleMessage(int formatVersion, const std::array<QString, 5>& hashes);
Result<QString> bundleHash(int formatVersion, const std::array<QString, 5>& hashes);
QByteArray chunkNonce(const QByteArray& prefix, quint32 index);
QByteArray chunkAad(quint32 index);
}
