#include <rutool/platform/FileOps.hpp>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStorageInfo>
#include <QMap>
#include <limits>
#include <windows.h>
#include <tlhelp32.h>

namespace rutool::platform {
namespace {
Error io(const QString& text) { return {ErrorCode::Io, text, QStringLiteral("Win32 error %1").arg(GetLastError())}; }
const wchar_t* wide(const QString& path) { return reinterpret_cast<const wchar_t*>(path.utf16()); }
struct Handle { HANDLE value{INVALID_HANDLE_VALUE}; ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } };
}
Result<void> requireGameClosed() {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (snapshot.value == INVALID_HANDLE_VALUE) return std::unexpected(io(QStringLiteral("Не удалось проверить, закрыта ли игра.")));
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.value, &entry)) return std::unexpected(io(QStringLiteral("Не удалось прочитать список процессов.")));
    do {
        if (QString::fromWCharArray(entry.szExeFile).compare(QStringLiteral("BlueArchive.exe"), Qt::CaseInsensitive) == 0)
            return std::unexpected(io(QStringLiteral("Закройте Blue Archive перед установкой или восстановлением.")));
    } while (Process32NextW(snapshot.value, &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) return std::unexpected(io(QStringLiteral("Проверка процессов не завершена.")));
    return {};
}
Result<void> requireSafeFile(const QString& path) {
    auto current = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    while (!current.isEmpty()) {
        const auto attributes = GetFileAttributesW(wide(current));
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return std::unexpected(io(QStringLiteral("Файл недоступен или находится за ссылкой/junction.")));
        const auto parent = QFileInfo(current).absolutePath();
        if (QDir::cleanPath(parent).compare(QDir::cleanPath(current), Qt::CaseInsensitive) == 0) break;
        current = QDir::toNativeSeparators(parent);
    }
    if (!QFileInfo(path).isFile()) return std::unexpected(io(QStringLiteral("Ожидается обычный файл.")));
    return {};
}
Result<void> requireSafeDirectory(const QString& path) {
    auto current = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    while (!current.isEmpty()) {
        const auto attributes = GetFileAttributesW(wide(current));
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return std::unexpected(io(QStringLiteral("Небезопасный каталог или junction.")));
        const auto parent = QFileInfo(current).absolutePath(); if (QDir::cleanPath(parent).compare(QDir::cleanPath(current), Qt::CaseInsensitive) == 0) break; current = QDir::toNativeSeparators(parent);
    }
    return {};
}
Result<void> flushFile(const QString& path) {
    const auto native = QDir::toNativeSeparators(path);
    Handle file{CreateFileW(wide(native), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE || !FlushFileBuffers(file.value)) return std::unexpected(io(QStringLiteral("Не удалось надёжно записать файл на диск.")));
    return {};
}
Result<void> requireFreeSpace(const std::vector<std::pair<QString, quint64>>& writes) {
    QMap<QByteArray, std::pair<QStorageInfo, quint64>> volumes;
    for (const auto& [path, bytes] : writes) {
        auto parent = QFileInfo(path).absoluteFilePath();
        while (!QFileInfo::exists(parent)) { const auto next = QFileInfo(parent).absolutePath(); if (next == parent) break; parent = next; }
        QStorageInfo storage(parent);
        if (!storage.isValid() || !storage.isReady() || storage.bytesAvailable() < 0)
            return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Не удалось проверить свободное место на диске."), parent});
        auto& volume = volumes[storage.device()]; volume.first = storage;
        if (bytes > std::numeric_limits<quint64>::max() - volume.second) return std::unexpected(io(QStringLiteral("Некорректный размер файлов.")));
        volume.second += bytes;
    }
    for (const auto& volume : volumes) {
        constexpr quint64 reserve = 64ULL * 1024 * 1024;
        const auto available = static_cast<quint64>(volume.first.bytesAvailable());
        if (available < reserve || volume.second > available - reserve)
            return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Недостаточно свободного места на диске %1. Требуется ещё %2 МБ с запасом.")
                .arg(volume.first.rootPath()).arg(volume.second / (1024 * 1024) + 64)});
    }
    return {};
}
Result<void> replaceFile(const QString& staged, const QString& target) {
    const auto source = QDir::toNativeSeparators(staged), destination = QDir::toNativeSeparators(target);
    if (!ReplaceFileW(wide(destination), wide(source), nullptr, 0, nullptr, nullptr))
        return std::unexpected(io(QStringLiteral("Не удалось атомарно заменить файл игры.")));
    return {};
}
Result<qint64> currentProcessStartedTicks() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return std::unexpected(io(QStringLiteral("Время запуска процесса недоступно.")));
    const auto ticks = (static_cast<quint64>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    return static_cast<qint64>(ticks + 504911232000000000ULL);
}
Result<qint64> launchHidden(const QString& program, const QStringList& arguments, const QString& workingDirectory) {
    QProcess process; process.setProgram(program); process.setArguments(arguments); process.setWorkingDirectory(workingDirectory);
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
    qint64 pid{}; if (!process.startDetached(&pid)) return std::unexpected(io(QStringLiteral("Не удалось запустить обновлятор.")));
    return pid;
}
FileLock::~FileLock() { if (handle_ && handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
Result<std::unique_ptr<FileLock>> FileLock::read(const QString& path, bool allowReplace) {
    auto safe = requireSafeFile(path);
    if (!safe) return std::unexpected(safe.error());
    const auto native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    std::unique_ptr<FileLock> lock(new FileLock);
    lock->handle_ = CreateFileW(wide(native), GENERIC_READ, FILE_SHARE_READ | (allowReplace ? FILE_SHARE_DELETE : 0U), nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (lock->handle_ == INVALID_HANDLE_VALUE) return std::unexpected(io(QStringLiteral("Файл занят другой операцией.")));
    return lock;
}
Result<std::unique_ptr<FileLock>> FileLock::operation(const QString& target) {
    auto safe = requireSafeFile(target);
    if (!safe) return std::unexpected(safe.error());
    const auto path = QDir::toNativeSeparators(target + QStringLiteral(".baretool.lock"));
    std::unique_ptr<FileLock> lock(new FileLock);
    lock->handle_ = CreateFileW(wide(path), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (lock->handle_ == INVALID_HANDLE_VALUE) return std::unexpected(io(QStringLiteral("Установку уже выполняет другой экземпляр программы.")));
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(lock->handle_, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return std::unexpected(io(QStringLiteral("Небезопасный файл блокировки.")));
    return lock;
}
Result<std::unique_ptr<FileLock>> FileLock::directoryLease(const QString& directory) {
    auto safe = requireSafeDirectory(directory); if (!safe) return std::unexpected(safe.error());
    const auto path = QDir::toNativeSeparators(QDir(directory).filePath(QStringLiteral(".lease")));
    std::unique_ptr<FileLock> lock(new FileLock);
    lock->handle_ = CreateFileW(wide(path), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (lock->handle_ == INVALID_HANDLE_VALUE) return std::unexpected(io(QStringLiteral("Каталог принадлежит другой операции.")));
    return lock;
}
Result<std::unique_ptr<FileLock>> FileLock::cacheEntry(const QString& target) {
    auto safe = requireSafeDirectory(QFileInfo(target).absolutePath()); if (!safe) return std::unexpected(safe.error());
    const auto path = QDir::toNativeSeparators(QFileInfo(target).absoluteFilePath() + QStringLiteral(".lease"));
    std::unique_ptr<FileLock> lock(new FileLock);
    lock->handle_ = CreateFileW(wide(path), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (lock->handle_ == INVALID_HANDLE_VALUE) return std::unexpected(io(QStringLiteral("Пакет используется другим экземпляром инсталлера.")));
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(lock->handle_, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
        return std::unexpected(io(QStringLiteral("Небезопасный файл блокировки кэша.")));
    return lock;
}
}
