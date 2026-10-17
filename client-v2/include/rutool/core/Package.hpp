#pragma once
#include <rutool/core/RutProtocol.hpp>
#include <rutool/platform/Crypto.hpp>
#include <QJsonObject>
#include <atomic>
#include <functional>
#include <memory>

namespace rutool {
using Cancellation = std::shared_ptr<std::atomic_bool>;
struct PreparedPackage {
    QString folder;
    QString source;
    QString sourceHash;
    QString originalHash;
    QString patchedHash;
    QString watermarkHash;
    std::array<QString, 5> bundleHashes{};
    bool bundle{};
    QJsonObject signature;
};
Result<QString> hashFile(const QString& path, const Cancellation& cancel = {});
Result<void> extractPackageZip(const QString& zip, const QString& newFolder, const Cancellation& cancel = {});
Result<PreparedPackage> decryptPackage(const QString& source, const QString& newFolder,
                                      const platform::Secret& password, const Cancellation& cancel = {});
Result<PreparedPackage> validatePreparedPackage(const QString& folder, const Cancellation& cancel = {});
}
