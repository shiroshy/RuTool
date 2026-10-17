#include <rutool/platform/Runtime.hpp>
#include <QFileInfo>
#include <windows.h>

namespace rutool::platform {
QString applicationRoot() {
    wchar_t path[32768]{};
    const auto count = GetModuleFileNameW(nullptr, path, 32768);
    return count && count < 32768 ? QFileInfo(QString::fromWCharArray(path)).absolutePath() : QString{};
}
}
