#pragma once
#include <rutool/platform/Crypto.hpp>

namespace rutool {
QString installerConfigPath(const QString& name);
Result<platform::Secret> readPackagePassword(const QString& path);
QByteArray updatePublicKey();
}
