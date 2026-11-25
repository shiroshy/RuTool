#pragma once
#include <rutool/platform/FileOps.hpp>
#include <QDir>
#include <QFile>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace rutool::platform::posix {
inline Error io(const QString& message) {
    return {ErrorCode::Io, message, QString::fromLocal8Bit(std::strerror(errno))};
}
struct Fd {
    int value{-1};
    explicit Fd(int fd = -1) : value(fd) {}
    ~Fd() { if (value >= 0) ::close(value); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept : value(std::exchange(other.value, -1)) {}
    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) { if (value >= 0) ::close(value); value = std::exchange(other.value, -1); }
        return *this;
    }
    explicit operator bool() const { return value >= 0; }
    int release() { return std::exchange(value, -1); }
};
// Resolve every parent with openat(O_NOFOLLOW); reject traversal before cleanPath
// can hide it. No directory in a write path may be a symbolic link.
inline Result<Fd> directory(const QString& path) {
    if (path.contains(QChar::Null) || path.split('/').contains(QStringLiteral("..")))
        return std::unexpected(io(QStringLiteral("Небезопасный путь.")));
    const auto absolute = QFileInfo(path).absoluteFilePath();
    Fd current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    for (const auto& part : absolute.split('/', Qt::SkipEmptyParts)) {
        if (part == QStringLiteral(".")) continue;
        const auto name = QFile::encodeName(part);
        Fd next(::openat(current.value, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (!next) return std::unexpected(io(QStringLiteral("Каталог недоступен или находится за символической ссылкой.")));
        current = std::move(next);
    }
    if (!current) return std::unexpected(io(QStringLiteral("Каталог недоступен.")));
    return current;
}
inline Result<Fd> file(const QString& path, int flags, mode_t mode = 0600) {
    if (path.contains(QChar::Null) || path.split('/').contains(QStringLiteral("..")))
        return std::unexpected(io(QStringLiteral("Небезопасный путь файла.")));
    auto parent = directory(QFileInfo(path).absolutePath());
    if (!parent) return std::unexpected(parent.error());
    const auto name = QFile::encodeName(QFileInfo(path).fileName());
    Fd result(::openat(parent->value, name.constData(), flags | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, mode));
    struct stat info{};
    if (!result || ::fstat(result.value, &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1)
        return std::unexpected(io(QStringLiteral("Ожидается обычный файл без символических и жёстких ссылок.")));
    return result;
}
}
