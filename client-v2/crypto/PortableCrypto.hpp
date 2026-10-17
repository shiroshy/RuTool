#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <mbedtls/sha256.h>

namespace rutool::crypto {
using Bytes = std::span<const unsigned char>;
using MutableBytes = std::span<unsigned char>;
void erase(MutableBytes bytes) noexcept;
bool pbkdf2Sha256(Bytes password, Bytes salt, std::uint32_t iterations, MutableBytes output);
bool decryptGcm(Bytes key, Bytes nonce, Bytes cipher, Bytes tag, Bytes aad, MutableBytes plain);
bool verifyP256(Bytes message, Bytes signatureP1363, Bytes publicSpki);
class Sha256 final {
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
    bool update(Bytes bytes);
    bool finish(std::array<unsigned char, 32>& digest);
private:
    mbedtls_sha256_context context_{};
    bool valid_{};
};
}
