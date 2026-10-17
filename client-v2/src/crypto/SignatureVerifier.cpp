#include <rutool/platform/SignatureVerifier.hpp>
#include "PortableCrypto.hpp"

namespace rutool::platform {
bool verifyP256(const QByteArray& message, const QByteArray& signature, const QByteArray& spki) {
    const auto view = [](const QByteArray& bytes) -> crypto::Bytes { return {reinterpret_cast<const unsigned char*>(bytes.constData()), static_cast<std::size_t>(bytes.size())}; };
    return crypto::verifyP256(view(message), view(signature), view(spki));
}
}
