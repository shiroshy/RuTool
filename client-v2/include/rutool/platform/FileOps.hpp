#pragma once
#include <rutool/core/RutProtocol.hpp>
#include <memory>
#include <QStringList>

namespace rutool::platform {
Result<void> requireGameClosed();
Result<void> requireSafeFile(const QString& path);
Result<void> requireSafeDirectory(const QString& path);
Result<void> flushFile(const QString& path);
Result<void> replaceFile(const QString& staged, const QString& target);
Result<qint64> currentProcessStartedTicks();
Result<qint64> launchHidden(const QString& program, const QStringList& arguments, const QString& workingDirectory);
class FileLock final {
public:
    ~FileLock();
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    static Result<std::unique_ptr<FileLock>> read(const QString& path, bool allowReplace = false);
    static Result<std::unique_ptr<FileLock>> operation(const QString& target);
    static Result<std::unique_ptr<FileLock>> directoryLease(const QString& directory);
    static Result<std::unique_ptr<FileLock>> cacheEntry(const QString& target);
private:
    FileLock() = default;
    void* handle_{};
};
}
