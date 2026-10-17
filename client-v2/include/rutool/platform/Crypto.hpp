#pragma once
#include <rutool/core/RutProtocol.hpp>
#include <memory>

namespace rutool::platform {
class Secret final {
public:
    explicit Secret(QByteArray bytes = {});
    ~Secret();
    Secret(Secret&&) noexcept;
    Secret& operator=(Secret&&) noexcept;
    Secret(const Secret&) = delete;
    Secret& operator=(const Secret&) = delete;
    QByteArray& bytes() { return bytes_; }
    const QByteArray& bytes() const { return bytes_; }
private:
    QByteArray bytes_;
};
Result<Secret> deriveRutKey(const Secret& password, const QByteArray& salt);
Result<Secret> decryptGcm(const Secret& key, const QByteArray& nonce, const QByteArray& cipher,
                          const QByteArray& tag, const QByteArray& aad);
Result<QByteArray> protectUser(const QByteArray& plain, const QByteArray& entropy);
Result<Secret> unprotectUser(const QByteArray& cipher, const QByteArray& entropy);
Result<QString> installationDeviceId();
}
