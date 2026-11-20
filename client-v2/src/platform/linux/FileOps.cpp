#include "Posix.hpp"
#include <QDateTime>
#include <QProcess>
#include <QStorageInfo>
#include <QMap>
#include <limits>
#include <sys/file.h>

namespace rutool::platform {
namespace {
Result<void> lockFd(int fd, int operation) {
    if (::flock(fd, operation | LOCK_NB) != 0)
        return std::unexpected(posix::io(QStringLiteral("Файл занят другой операцией.")));
    return {};
}
void* encode(int fd) { return reinterpret_cast<void*>(static_cast<intptr_t>(fd) + 1); }
int decode(void* handle) { return static_cast<int>(reinterpret_cast<intptr_t>(handle) - 1); }
}
Result<void> requireGameClosed() {
    const QDir proc(QStringLiteral("/proc"));
    if (!proc.isReadable() || !QFileInfo::exists(QStringLiteral("/proc/self/stat")))
        return std::unexpected(posix::io(QStringLiteral("Не удалось прочитать список процессов.")));
    for (const auto& pid : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool numeric{}; pid.toULongLong(&numeric); if (!numeric) continue;
        QFile command(proc.filePath(pid + QStringLiteral("/cmdline")));
        QFile comm(proc.filePath(pid + QStringLiteral("/comm")));
        QByteArray bytes;
        if (command.open(QIODevice::ReadOnly)) bytes = command.read(1024 * 1024);
        else if (QFileInfo::exists(proc.filePath(pid)) && QFileInfo(proc.filePath(pid)).ownerId() == ::getuid())
            return std::unexpected(posix::io(QStringLiteral("Не удалось проверить процесс текущего пользователя.")));
        if (comm.open(QIODevice::ReadOnly)) bytes += '\0' + comm.read(4096).trimmed();
        for (const auto& argument : bytes.split('\0')) {
            const auto name = QString::fromLocal8Bit(argument).replace('\\', '/').section('/', -1).trimmed();
            if (name.compare(QStringLiteral("BlueArchive.exe"), Qt::CaseInsensitive) == 0 ||
                name.compare(QStringLiteral("BlueArchive"), Qt::CaseInsensitive) == 0)
                return std::unexpected(posix::io(QStringLiteral("Закройте Blue Archive перед установкой или восстановлением.")));
        }
    }
    return {};
}
Result<void> requireSafeFile(const QString& path) {
    auto result = posix::file(path, O_RDONLY);
    if (!result) return std::unexpected(result.error());
    return {};
}
Result<void> requireSafeDirectory(const QString& path) {
    auto result = posix::directory(path);
    if (!result) return std::unexpected(result.error());
    return {};
}
Result<void> flushFile(const QString& path) {
    auto file = posix::file(path, O_RDONLY);
    if (!file) return std::unexpected(file.error());
    if (::fsync(file->value) != 0) return std::unexpected(posix::io(QStringLiteral("Не удалось записать файл на диск.")));
    auto parent = posix::directory(QFileInfo(path).absolutePath());
    if (!parent) return std::unexpected(parent.error());
    if (::fsync(parent->value) != 0) return std::unexpected(posix::io(QStringLiteral("Не удалось записать каталог на диск.")));
    return {};
}
Result<void> requireFreeSpace(const std::vector<std::pair<QString, quint64>>& writes) {
    QMap<QByteArray, std::pair<QStorageInfo, quint64>> volumes;
    for (const auto& [path, bytes] : writes) {
        auto parent = QFileInfo(path).absoluteFilePath();
        while (!QFileInfo::exists(parent)) { const auto next = QFileInfo(parent).absolutePath(); if (next == parent) break; parent = next; }
        QStorageInfo storage(parent);
        if (!storage.isValid() || !storage.isReady() || storage.isReadOnly() || storage.bytesAvailable() < 0)
            return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Не удалось проверить доступное место на диске."), parent});
        auto& volume = volumes[storage.device()]; volume.first = storage;
        if (bytes > std::numeric_limits<quint64>::max() - volume.second)
            return std::unexpected(Error{ErrorCode::InvalidInput, QStringLiteral("Некорректный размер файлов.")});
        volume.second += bytes;
    }
    for (const auto& volume : volumes) {
        constexpr quint64 reserve = 64ULL * 1024 * 1024;
        const auto available = static_cast<quint64>(volume.first.bytesAvailable());
        if (available < reserve || volume.second > available - reserve)
            return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Недостаточно свободного места на диске.")});
    }
    return {};
}
Result<void> replaceFile(const QString& staged, const QString& target) {
    auto source = posix::file(staged, O_RDONLY), destination = posix::file(target, O_RDONLY);
    if (!source) return std::unexpected(source.error());
    if (!destination) return std::unexpected(destination.error());
    auto from = posix::directory(QFileInfo(staged).absolutePath()), to = posix::directory(QFileInfo(target).absolutePath());
    if (!from) return std::unexpected(from.error());
    if (!to) return std::unexpected(to.error());
    const auto a = QFile::encodeName(QFileInfo(staged).fileName()), b = QFile::encodeName(QFileInfo(target).fileName());
    struct stat original{};
    if (::fstat(destination->value, &original) != 0 || ::fchmod(source->value, original.st_mode & 0777) != 0)
        return std::unexpected(posix::io(QStringLiteral("Не удалось сохранить права файла игры.")));
    if (::fsync(source->value) != 0 || ::renameat(from->value, a.constData(), to->value, b.constData()) != 0)
        return std::unexpected(posix::io(QStringLiteral("Не удалось атомарно заменить файл игры.")));
    if (::fsync(to->value) != 0 || ::fsync(from->value) != 0)
        return std::unexpected(posix::io(QStringLiteral("Не удалось сохранить замену на диск; требуется восстановление.")));
    return {};
}
Result<qint64> currentProcessStartedTicks() {
    QFile stat(QStringLiteral("/proc/self/stat"));
    if (!stat.open(QIODevice::ReadOnly)) return std::unexpected(posix::io(QStringLiteral("Время запуска процесса недоступно.")));
    const auto bytes = stat.readAll(); const auto fields = bytes.mid(bytes.lastIndexOf(')') + 2).split(' ');
    if (fields.size() <= 19) return std::unexpected(posix::io(QStringLiteral("Некорректный /proc/self/stat.")));
    bool ok{}; const auto ticks = fields[19].toLongLong(&ok);
    if (!ok) return std::unexpected(posix::io(QStringLiteral("Некорректное время запуска.")));
    return ticks; // Linux boot-relative clock ticks; Windows update protocol is disabled.
}
Result<qint64> launchHidden(const QString& program, const QStringList& arguments, const QString& workingDirectory) {
    qint64 pid{};
    if (!QProcess::startDetached(program, arguments, workingDirectory, &pid))
        return std::unexpected(posix::io(QStringLiteral("Не удалось запустить приложение.")));
    return pid;
}
FileLock::~FileLock() { if (handle_) ::close(decode(handle_)); }
Result<std::unique_ptr<FileLock>> FileLock::read(const QString& path, bool allowReplace) {
    Q_UNUSED(allowReplace)
    auto fd = posix::file(path, O_RDONLY); if (!fd) return std::unexpected(fd.error());
    auto locked = lockFd(fd->value, LOCK_SH); if (!locked) return std::unexpected(locked.error());
    std::unique_ptr<FileLock> lock(new FileLock); lock->handle_ = encode(fd->release()); return lock;
}
Result<std::unique_ptr<FileLock>> FileLock::operation(const QString& target) {
    auto safe = requireSafeFile(target); if (!safe) return std::unexpected(safe.error());
    auto fd = posix::file(target + QStringLiteral(".baretool.lock"), O_RDWR | O_CREAT);
    if (!fd) return std::unexpected(fd.error());
    auto locked = lockFd(fd->value, LOCK_EX); if (!locked) return std::unexpected(locked.error());
    std::unique_ptr<FileLock> lock(new FileLock); lock->handle_ = encode(fd->release()); return lock;
}
Result<std::unique_ptr<FileLock>> FileLock::directoryLease(const QString& directory) {
    auto fd = posix::directory(directory); if (!fd) return std::unexpected(fd.error());
    auto locked = lockFd(fd->value, LOCK_EX); if (!locked) return std::unexpected(locked.error());
    std::unique_ptr<FileLock> lock(new FileLock); lock->handle_ = encode(fd->release()); return lock;
}
Result<std::unique_ptr<FileLock>> FileLock::cacheEntry(const QString& target) {
    auto fd = posix::file(target + QStringLiteral(".lease"), O_RDWR | O_CREAT);
    if (!fd) return std::unexpected(fd.error());
    auto locked = lockFd(fd->value, LOCK_EX); if (!locked) return std::unexpected(locked.error());
    // Persistent lock inodes avoid the unlink/recreate race between three processes.
    std::unique_ptr<FileLock> lock(new FileLock); lock->handle_ = encode(fd->release()); return lock;
}
}
