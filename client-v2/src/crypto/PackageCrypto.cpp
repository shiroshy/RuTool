#include <rutool/platform/Crypto.hpp>
#include "PortableCrypto.hpp"
#include <utility>

namespace rutool::platform {
namespace {
Error failure(const QString& message) { return {ErrorCode::Signature, message, QStringLiteral("Mbed TLS")}; }
crypto::Bytes view(const QByteArray& bytes) { return {reinterpret_cast<const unsigned char*>(bytes.constData()), static_cast<std::size_t>(bytes.size())}; }
crypto::MutableBytes edit(QByteArray& bytes) { return {reinterpret_cast<unsigned char*>(bytes.data()), static_cast<std::size_t>(bytes.size())}; }
}
Secret::Secret(QByteArray bytes) : bytes_(std::move(bytes)) { bytes_.detach(); }
Secret::~Secret() { crypto::erase(edit(bytes_)); }
Secret::Secret(Secret&& other) noexcept : bytes_(std::move(other.bytes_)) {}
Secret& Secret::operator=(Secret&& other) noexcept {
    if (this != &other) { crypto::erase(edit(bytes_)); bytes_ = std::move(other.bytes_); }
    return *this;
}
Result<Secret> deriveRutKey(const Secret& password, const QByteArray& salt) {
    if (salt.size() != 16 || password.bytes().isEmpty() || password.bytes().size() > 4096)
        return std::unexpected(failure(QStringLiteral("Некорректный ключ пакета.")));
    Secret key(QByteArray(32, '\0'));
    if (!crypto::pbkdf2Sha256(view(password.bytes()), view(salt), 310000, edit(key.bytes())))
        return std::unexpected(failure(QStringLiteral("Не удалось вычислить ключ пакета.")));
    return key;
}
Result<Secret> decryptGcm(const Secret& key, const QByteArray& nonce, const QByteArray& cipher,
                          const QByteArray& tag, const QByteArray& aad) {
    if (key.bytes().size() != 32 || nonce.size() != 12 || tag.size() != 16 || cipher.size() > 1024 * 1024)
        return std::unexpected(failure(QStringLiteral("Некорректный блок AES-GCM.")));
    Secret plain(QByteArray(cipher.size(), '\0'));
    if (!crypto::decryptGcm(view(key.bytes()), view(nonce), view(cipher), view(tag), view(aad), edit(plain.bytes())))
        return std::unexpected(failure(QStringLiteral("Пакет повреждён или не прошёл проверку AES-GCM.")));
    return plain;
}
}
