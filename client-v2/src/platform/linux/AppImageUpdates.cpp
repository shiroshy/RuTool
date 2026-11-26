#include <rutool/platform/AppImageUpdates.hpp>
#include <rutool/platform/Runtime.hpp>
#include "Posix.hpp"
#include <QCoreApplication>
#include <QSaveFile>
#include <linux/fs.h>
#include <sys/syscall.h>

namespace rutool::platform {
namespace {
Error failure(const QString& message) { return {ErrorCode::Io, message}; }
Result<void> exactHash(const QString& path, const QString& expected, const Cancellation& cancel) {
    auto actual = hashFile(path, cancel);
    if (!actual) return std::unexpected(actual.error());
    if (*actual != expected) return std::unexpected(failure(QStringLiteral("AppImage изменился во время обновления.")));
    return {};
}
bool appImage(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto header = file.read(64);
    return header.size() == 64 && header.startsWith(QByteArray("\x7f" "ELF", 4)) &&
        header[4] == 2 && header[5] == 1 && header.mid(8, 3) == QByteArray("AI\2", 3) &&
        header.mid(18, 2) == QByteArray("\x3e\0", 2);
}
}
Result<QString> runningAppImage() {
    const auto image = QFileInfo(QString::fromLocal8Bit(qgetenv("APPIMAGE"))).canonicalFilePath();
    const auto appDir = QFileInfo(QString::fromLocal8Bit(qgetenv("APPDIR"))).canonicalFilePath();
    if (image.isEmpty() || appDir.isEmpty() ||
        QFileInfo(QDir(appDir).filePath(QStringLiteral("usr/bin"))).canonicalFilePath() != QFileInfo(applicationRoot()).canonicalFilePath() ||
        !requireSafeFile(image) || !appImage(image))
        return std::unexpected(failure(QStringLiteral("Автообновление доступно при запуске RuTool из AppImage.")));
    const auto expected = QCoreApplication::instance()->property("rutoolAppImageHash").toString();
    if (!isHash(expected) || !exactHash(image, expected, {}))
        return std::unexpected(failure(QStringLiteral("AppImage изменён после запуска. Перезапустите RuTool перед обновлением.")));
    return image;
}
Result<QString> installAppImageUpdate(const LinuxRelease& release, const QString& current,
    const QString& destination, const QString& expectedCurrentHash, const Cancellation& cancel, const TransferProgress& progress, QNetworkAccessManager* transport) {
    const auto canonicalParent = QFileInfo(QFileInfo(destination).absolutePath()).canonicalFilePath();
    if (destination.contains(QChar::Null) || canonicalParent.isEmpty() || QFileInfo(destination).fileName().isEmpty())
        return std::unexpected(failure(QStringLiteral("Выберите существующий каталог для AppImage.")));
    const auto target = QDir(canonicalParent).filePath(QFileInfo(destination).fileName());
    const auto original = QFileInfo(current).absoluteFilePath();
    if (!requireSafeFile(original) || !appImage(original) || !isHash(expectedCurrentHash) || !isHash(release.sha256) || release.bytes < 64)
        return std::unexpected(failure(QStringLiteral("Некорректный AppImage или параметры обновления.")));
    const bool replacing = target == original;
    if (!replacing && (QFileInfo::exists(target) || QFileInfo(target).isSymLink()))
        return std::unexpected(failure(QStringLiteral("Для новой копии RuTool выберите свободное имя файла.")));
    const auto parent = QFileInfo(target).absolutePath();
    auto directory = posix::directory(parent); if (!directory) return std::unexpected(directory.error());
    auto lease = FileLock::directoryLease(parent); if (!lease) return std::unexpected(lease.error());
    auto before = hashFile(original, cancel); if (!before) return std::unexpected(before.error());
    if (*before != expectedCurrentHash)
        return std::unexpected(failure(QStringLiteral("AppImage изменён после запуска. Перезапустите RuTool перед обновлением.")));
    const auto stage = target + QStringLiteral(".rutool-update-") + release.sha256 + QStringLiteral(".AppImage");
    auto space = requireFreeSpace({{target, static_cast<quint64>(release.bytes) + static_cast<quint64>(QFileInfo(original).size())}});
    if (!space) return std::unexpected(space.error());
    auto downloaded = downloadAsset(release.url, release.bytes, release.sha256, stage, cancel, progress, transport);
    if (!downloaded) return std::unexpected(downloaded.error());
    if (!appImage(stage) || !QFile::setPermissions(stage, QFile::permissions(original)))
        return std::unexpected(failure(QStringLiteral("Скачанный файл не является исполняемым AppImage x86_64.")));
    const auto backup = target + QStringLiteral(".rutool-previous.AppImage");
    if (replacing) {
        if ((QFileInfo::exists(backup) || QFileInfo(backup).isSymLink()) && !requireSafeFile(backup))
            return std::unexpected(failure(QStringLiteral("Небезопасный путь предыдущей версии AppImage.")));
        QFile source(original); QSaveFile copy(backup);
        if (!source.open(QIODevice::ReadOnly) || !copy.open(QIODevice::WriteOnly) || !copy.setPermissions(QFile::permissions(original)))
            return std::unexpected(failure(QStringLiteral("Не удалось сохранить предыдущий AppImage.")));
        while (!source.atEnd()) {
            if (cancel && cancel->load()) return std::unexpected(Error{ErrorCode::Cancelled, QStringLiteral("Обновление отменено до замены приложения.")});
            const auto bytes = source.read(1024 * 1024);
            if ((bytes.isEmpty() && source.error() != QFileDevice::NoError) || copy.write(bytes) != bytes.size())
                return std::unexpected(failure(QStringLiteral("Не удалось скопировать предыдущий AppImage.")));
        }
        source.close();
        if (!copy.commit()) return std::unexpected(failure(QStringLiteral("Не удалось сохранить предыдущий AppImage.")));
        auto flushed = flushFile(backup); if (!flushed) return std::unexpected(flushed.error());
        auto checked = exactHash(backup, *before, cancel); if (!checked) return std::unexpected(checked.error());
    }
    auto unchanged = exactHash(original, *before, cancel); if (!unchanged) return std::unexpected(unchanged.error());
    auto checked = exactHash(stage, release.sha256, cancel); if (!checked) return std::unexpected(checked.error());
    if (cancel && cancel->load()) return std::unexpected(Error{ErrorCode::Cancelled, QStringLiteral("Обновление отменено до замены приложения.")});
    if (replacing) {
        auto replaced = replaceFile(stage, target);
        if (!replaced) return std::unexpected(Error{ErrorCode::Io,
            QStringLiteral("Замена AppImage не завершена. Предыдущая версия сохранена: %1").arg(backup), replaced.error().cause.message});
    } else {
        auto flushed = flushFile(stage); if (!flushed) return std::unexpected(flushed.error());
        const auto from = QFile::encodeName(QFileInfo(stage).fileName()), to = QFile::encodeName(QFileInfo(target).fileName());
        if (::syscall(SYS_renameat2, directory->value, from.constData(), directory->value, to.constData(), RENAME_NOREPLACE) != 0 || ::fsync(directory->value) != 0)
            return std::unexpected(posix::io(QStringLiteral("Не удалось сохранить новую копию AppImage. Исходная версия не изменена.")));
    }
    checked = exactHash(target, release.sha256, {}); if (!checked) return std::unexpected(checked.error());
    return target;
}
}
