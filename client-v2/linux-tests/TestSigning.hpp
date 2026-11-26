#pragma once
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <mbedtls/ecdsa.h>

namespace rutool::test {
struct SignedFixture { QByteArray envelope, publicKey; };
// Known private scalars 1 and 2 are public test data, never production keys.
inline SignedFixture signFixture(const QJsonObject& object, int scalar = 1) {
    mbedtls_ecp_group group; mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point point; mbedtls_ecp_point_init(&point);
    mbedtls_mpi d, r, s; mbedtls_mpi_init(&d); mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
    unsigned counter = 1;
    const auto random = [](void* context, unsigned char* output, size_t size) -> int {
        auto& value = *static_cast<unsigned*>(context);
        for (size_t i = 0; i < size; ++i) output[i] = static_cast<unsigned char>((value++ % 251) + 1);
        return 0;
    };
    const auto payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    const auto digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    QByteArray signature(64, '\0'), publicPoint(65, '\0'); size_t written{};
    const bool ok = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_mpi_lset(&d, scalar) == 0 &&
        mbedtls_ecp_mul(&group, &point, &d, &group.G, random, &counter) == 0 &&
        mbedtls_ecp_point_write_binary(&group, &point, MBEDTLS_ECP_PF_UNCOMPRESSED, &written,
            reinterpret_cast<unsigned char*>(publicPoint.data()), 65) == 0 && written == 65 &&
        mbedtls_ecdsa_sign(&group, &r, &s, &d, reinterpret_cast<const unsigned char*>(digest.constData()), 32, random, &counter) == 0 &&
        mbedtls_mpi_write_binary(&r, reinterpret_cast<unsigned char*>(signature.data()), 32) == 0 &&
        mbedtls_mpi_write_binary(&s, reinterpret_cast<unsigned char*>(signature.data()) + 32, 32) == 0;
    mbedtls_ecp_group_free(&group); mbedtls_ecp_point_free(&point);
    mbedtls_mpi_free(&d); mbedtls_mpi_free(&r); mbedtls_mpi_free(&s);
    if (!ok) return {};
    return {QJsonDocument(QJsonObject{{QStringLiteral("PayloadBase64"), QString::fromLatin1(payload.toBase64())},
        {QStringLiteral("SignatureBase64"), QString::fromLatin1(signature.toBase64())}}).toJson(QJsonDocument::Compact),
        QByteArray::fromHex("3059301306072a8648ce3d020106082a8648ce3d030107034200") + publicPoint};
}
}
