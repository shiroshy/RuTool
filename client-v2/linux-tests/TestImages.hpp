#pragma once
#include <QByteArray>

namespace rutool::test {
inline QByteArray image(const QByteArray& label) {
    QByteArray bytes(64, '\0'); bytes.replace(0, 4, QByteArray("\x7f" "ELF", 4));
    bytes[4] = 2; bytes[5] = 1; bytes.replace(8, 3, QByteArray("AI\2", 3)); bytes[18] = 0x3e;
    return bytes + label;
}
}
