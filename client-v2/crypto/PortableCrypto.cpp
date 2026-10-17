#include "PortableCrypto.hpp"
#include <mbedtls/ecdsa.h>
#include <mbedtls/gcm.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/platform_util.h>
#include <algorithm>
#include <limits>

namespace rutool::crypto {
void erase(MutableBytes bytes) noexcept { if (!bytes.empty()) mbedtls_platform_zeroize(bytes.data(), bytes.size()); }
bool pbkdf2Sha256(Bytes password, Bytes salt, std::uint32_t iterations, MutableBytes output) {
    if (!iterations || output.empty() || output.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    const auto result = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, password.data(), password.size(), salt.data(), salt.size(), iterations,
        static_cast<std::uint32_t>(output.size()), output.data());
    if (result) erase(output);
    return result == 0;
}
bool decryptGcm(Bytes key, Bytes nonce, Bytes cipher, Bytes tag, Bytes aad, MutableBytes plain) {
    if (key.size() != 32 || nonce.size() != 12 || tag.size() != 16 || plain.size() != cipher.size()) return false;
    mbedtls_gcm_context context; mbedtls_gcm_init(&context);
    const auto setup = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key.data(), 256);
    const auto result = setup ? setup : mbedtls_gcm_auth_decrypt(&context, cipher.size(), nonce.data(), nonce.size(), aad.data(), aad.size(), tag.data(), tag.size(), cipher.data(), plain.data());
    mbedtls_gcm_free(&context);
    if (result) erase(plain);
    return result == 0;
}
bool verifyP256(Bytes message, Bytes signature, Bytes spki) {
    constexpr std::array<unsigned char, 27> prefix{0x30,0x59,0x30,0x13,0x06,0x07,0x2a,0x86,0x48,0xce,0x3d,0x02,0x01,0x06,0x08,0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07,0x03,0x42,0x00,0x04};
    if (signature.size() != 64 || spki.size() != prefix.size() + 64 || !std::equal(prefix.begin(), prefix.end(), spki.begin())) return false;
    std::array<unsigned char, 32> digest{}; Sha256 hash;
    if (!hash.update(message) || !hash.finish(digest)) return false;
    mbedtls_ecp_group group; mbedtls_ecp_point point; mbedtls_mpi r, s;
    mbedtls_ecp_group_init(&group); mbedtls_ecp_point_init(&point); mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
    const bool valid = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_ecp_point_read_binary(&group, &point, spki.data() + prefix.size() - 1, 65) == 0 &&
        mbedtls_ecp_check_pubkey(&group, &point) == 0 && mbedtls_mpi_read_binary(&r, signature.data(), 32) == 0 &&
        mbedtls_mpi_read_binary(&s, signature.data() + 32, 32) == 0 &&
        mbedtls_ecdsa_verify(&group, digest.data(), digest.size(), &point, &r, &s) == 0;
    mbedtls_mpi_free(&s); mbedtls_mpi_free(&r); mbedtls_ecp_point_free(&point); mbedtls_ecp_group_free(&group);
    return valid;
}
Sha256::Sha256() { mbedtls_sha256_init(&context_); valid_ = mbedtls_sha256_starts(&context_, 0) == 0; }
Sha256::~Sha256() { mbedtls_sha256_free(&context_); }
bool Sha256::update(Bytes bytes) { valid_ = valid_ && mbedtls_sha256_update(&context_, bytes.data(), bytes.size()) == 0; return valid_; }
bool Sha256::finish(std::array<unsigned char, 32>& digest) {
    const bool ok = valid_ && mbedtls_sha256_finish(&context_, digest.data()) == 0;
    valid_ = false; if (!ok) digest.fill(0); return ok;
}
}
