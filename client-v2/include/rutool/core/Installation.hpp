#pragma once
#include <rutool/core/Package.hpp>
#include <vector>
#include <optional>

namespace rutool {
QString gameDatabasePath(const QString& game);
struct InstallationRequest {
    QString game;
    PreparedPackage package;
    QString backupRoot;
    QString previousOriginal;
    std::vector<QString> approvedBefore;
    QString approvedAfter;
    bool originalVerified{};
};
struct InstallationResult { QString originalBackup; bool alreadyInstalled{}; };
struct FileReplacement { QString target; QString source; QString before; QString after; QString backup; };
using CommitGuard = std::function<Result<void>()>;
Result<std::vector<QString>> installationHashes(const QString& game, bool bundle, const Cancellation& cancel = {});
Result<InstallationResult> installPackage(const InstallationRequest& request, const CommitGuard& guard, const Cancellation& cancel = {});
Result<void> restoreOriginal(const QString& game, const QString& backup, const Cancellation& cancel = {});
Result<std::vector<FileReplacement>> originalReceipt(const QString& game, const QString& backup, const Cancellation& cancel = {});
Result<QString> findOriginalBackup(const QString& game, const QString& backupRoot, const Cancellation& cancel = {});
Result<std::optional<QString>> recoverInstallation(const QString& game);
}
