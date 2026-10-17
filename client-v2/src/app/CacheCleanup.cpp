#include "CacheCleanup.hpp"
#include <rutool/platform/FileOps.hpp>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

bool isTranslationDownload(const QString& dataRoot, const QString& path) {
    static const QRegularExpression name(QStringLiteral("^[0-9a-f]{32}[.]rut$"));
    const auto base = QFileInfo(QDir(dataRoot).filePath(QStringLiteral("TranslationDownloads"))).absoluteFilePath();
    return !path.isEmpty() && name.match(QFileInfo(path).fileName()).hasMatch()
        && QFileInfo(path).absolutePath().compare(base, Qt::CaseInsensitive) == 0;
}
void cleanupTranslationDownloads(const QString& dataRoot, const QString& preservedPath) {
    const auto base = QDir(dataRoot).filePath(QStringLiteral("TranslationDownloads"));
    if (!rutool::platform::requireSafeDirectory(base)) return;
    static const QRegularExpression name(QStringLiteral("^([0-9a-f]{32}[.]rut)(?:[.]partial)?$"));
    for (const auto& entry : QDir(base).entryInfoList(QDir::Files | QDir::Hidden | QDir::System)) {
        const auto match = name.match(entry.fileName()); if (!match.hasMatch()) continue;
        const auto target = QDir(base).filePath(match.captured(1));
        if (!isTranslationDownload(dataRoot, target)) continue;
        if (entry.fileName() == match.captured(1) && isTranslationDownload(dataRoot, preservedPath)
            && QFileInfo(target).absoluteFilePath().compare(QFileInfo(preservedPath).absoluteFilePath(), Qt::CaseInsensitive) == 0) continue;
        auto lease = rutool::platform::FileLock::cacheEntry(target); if (!lease) continue;
        if (rutool::platform::requireSafeFile(entry.absoluteFilePath())) QFile::remove(entry.absoluteFilePath());
    }
}

namespace {
bool collect(const QString& root, QStringList& files, QStringList& directories) {
    if (!rutool::platform::requireSafeDirectory(root)) return false;
    const auto entries = QDir(root).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const auto& entry : entries) {
        if (entry.fileName() == QStringLiteral(".lease")) continue;
        if (entry.isDir()) { if (!collect(entry.absoluteFilePath(), files, directories)) return false; directories.append(entry.absoluteFilePath()); }
        else { if (!rutool::platform::requireSafeFile(entry.absoluteFilePath())) return false; files.append(entry.absoluteFilePath()); }
    }
    return true;
}
}
void cleanupAbandonedUpdates(const QString& dataRoot) {
    const auto base = QDir(dataRoot).filePath(QStringLiteral("ApplicationUpdates"));
    if (!rutool::platform::requireSafeDirectory(base)) return;
    static const QRegularExpression id(QStringLiteral("^[0-9a-f]{32}$"));
    for (const auto& entry : QDir(base).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!id.match(entry.fileName()).hasMatch()) continue;
        const auto root = entry.absoluteFilePath();
        if (QFileInfo(root).absolutePath().compare(QFileInfo(base).absoluteFilePath(), Qt::CaseInsensitive) != 0) continue;
        auto lease = rutool::platform::FileLock::directoryLease(root); if (!lease) continue;
        QFile owner(QDir(root).filePath(QStringLiteral("owner.json")));
        if (!owner.open(QIODevice::ReadOnly) || owner.size() > 4096) continue;
        const auto object = QJsonDocument::fromJson(owner.readAll()).object(); owner.close();
        if (object.value(QStringLiteral("ProcessId")).toInteger() <= 0 || object.value(QStringLiteral("ProcessStartedUtcTicks")).toInteger() <= 0) continue;
        QStringList files, directories; if (!collect(root, files, directories)) continue;
        std::stable_sort(files.begin(), files.end(), [](const QString& a, const QString& b) { return QFileInfo(a).fileName() != QStringLiteral("owner.json") && QFileInfo(b).fileName() == QStringLiteral("owner.json"); });
        bool removed = true; for (const auto& file : files) if (!QFile::remove(file)) { removed = false; break; }
        if (!removed) continue;
        std::sort(directories.begin(), directories.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
        for (const auto& directory : directories) QDir().rmdir(directory);
        lease->reset(); QDir().rmdir(root);
    }
}
