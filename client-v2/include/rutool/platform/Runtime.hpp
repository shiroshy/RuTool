#pragma once
#include <rutool/core/RutProtocol.hpp>
#include <QStringList>

namespace rutool::platform {
QString applicationRoot();
#ifdef Q_OS_WIN
inline constexpr auto pathCaseSensitivity = Qt::CaseInsensitive;
#else
inline constexpr auto pathCaseSensitivity = Qt::CaseSensitive;
#endif
inline QString pathKey(QString path) {
#ifdef Q_OS_WIN
    return path.toCaseFolded();
#else
    return path;
#endif
}
#ifdef Q_OS_LINUX
QStringList discoverGameInstallations(const QStringList& steamRoots = {});
#endif
}
