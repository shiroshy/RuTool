#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace rutool::test {
inline QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
inline bool write(const QString& path, const QByteArray& bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
inline QString fixture(const char* name) {
    return QDir(QStringLiteral(RUTOOL_TEST_FIXTURES)).filePath(QString::fromLatin1(name));
}
}
