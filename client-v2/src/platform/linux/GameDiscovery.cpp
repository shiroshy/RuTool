#include <rutool/platform/Runtime.hpp>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace rutool::platform {
namespace {
QString readText(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.size() <= 4 * 1024 * 1024 ? QString::fromUtf8(file.readAll()) : QString{};
}
QString unescape(QString text) { return text.replace(QStringLiteral("\\\\"), QStringLiteral("\\")).replace(QStringLiteral("\\\""), QStringLiteral("\"")); }
bool game(const QString& path) {
    return QFileInfo(QDir(path).filePath(QStringLiteral("BlueArchive_Data/StreamingAssets/PUB/Resource/Preload/TableBundles/ExcelDB.db"))).isFile();
}
}
QStringList discoverGameInstallations(const QStringList& steamRoots) {
    QStringList roots = steamRoots;
    if (roots.isEmpty()) {
        const QDir home(QDir::homePath());
        roots = {home.filePath(QStringLiteral(".steam/steam")), home.filePath(QStringLiteral(".steam/root")),
            QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(QStringLiteral("Steam")),
            home.filePath(QStringLiteral(".var/app/com.valvesoftware.Steam/data/Steam"))};
    }
    QSet<QString> libraries;
    static const QRegularExpression library(QStringLiteral("\"(?:path|[0-9]+)\"\\s*\"((?:\\\\.|[^\"\\\\])*)\""));
    for (const auto& root : roots) {
        const auto resolved = QFileInfo(root).canonicalFilePath(); if (resolved.isEmpty()) continue;
        libraries.insert(resolved);
        auto matches = library.globalMatch(readText(QDir(root).filePath(QStringLiteral("steamapps/libraryfolders.vdf"))));
        while (matches.hasNext()) {
            const auto path = unescape(matches.next().captured(1));
            if (QDir::isAbsolutePath(path) && QFileInfo(QDir(path).filePath(QStringLiteral("steamapps"))).isDir())
                libraries.insert(QFileInfo(path).canonicalFilePath());
        }
    }
    QSet<QString> found;
    const auto add = [&found](const QString& path) {
        if (game(path)) { const auto canonical = QFileInfo(path).canonicalFilePath(); if (!canonical.isEmpty()) found.insert(canonical); }
    };
    static const QRegularExpression install(QStringLiteral("\"installdir\"\\s*\"((?:\\\\.|[^\"\\\\])*)\""), QRegularExpression::CaseInsensitiveOption);
    for (const auto& libraryRoot : libraries) {
        const QDir apps(QDir(libraryRoot).filePath(QStringLiteral("steamapps")));
        for (const auto& manifest : apps.entryList({QStringLiteral("appmanifest_*.acf")}, QDir::Files)) {
            const auto match = install.match(readText(apps.filePath(manifest))); if (!match.hasMatch()) continue;
            const auto name = unescape(match.captured(1));
            if (name.contains('/') || name.contains('\\') || name == QStringLiteral("..")) continue;
            add(apps.filePath(QStringLiteral("common/") + name));
        }
        // Steam/Proton installs normally live in common, not inside the prefix.
        const QDir common(apps.filePath(QStringLiteral("common")));
        for (const auto& name : common.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) add(common.filePath(name));
        // Also cover manually installed copies within Steam-managed Proton prefixes.
        const QDir compat(apps.filePath(QStringLiteral("compatdata")));
        for (const auto& id : compat.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QDir drive(compat.filePath(id + QStringLiteral("/pfx/drive_c")));
            for (const auto& base : {QString{}, QStringLiteral("Program Files"), QStringLiteral("Program Files (x86)"), QStringLiteral("Games")}) {
                const QDir folder(drive.filePath(base));
                add(folder.filePath(QStringLiteral("Blue Archive"))); add(folder.filePath(QStringLiteral("BlueArchive")));
                const QDir steam(folder.filePath(QStringLiteral("Steam/steamapps/common")));
                for (const auto& name : steam.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) add(steam.filePath(name));
            }
        }
    }
    auto result = found.values(); result.sort(); return result;
}
}
