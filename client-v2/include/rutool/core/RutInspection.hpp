#pragma once
#include <rutool/core/RutProtocol.hpp>
#include <atomic>
#include <memory>

namespace rutool {
struct RutInspection { quint64 bytes{}; quint64 chunks{}; quint64 ciphertextBytes{}; QString sha256; };
Result<RutInspection> inspectRut(const QString& path, const std::shared_ptr<std::atomic_bool>& cancel);
}
