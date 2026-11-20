#include <rutool/platform/Runtime.hpp>
#include <QCoreApplication>
namespace rutool::platform {
QString applicationRoot() { return QCoreApplication::applicationDirPath(); }
}
