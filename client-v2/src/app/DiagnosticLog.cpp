#include "DiagnosticLog.hpp"
#include <QDateTime>
#include <QSysInfo>
#include <QSaveFile>
#include <QRegularExpression>
#include <QDir>

QString DiagnosticLog::sanitize(QString text) {
    text.replace(QDir::homePath(), QStringLiteral("<USER>"), Qt::CaseInsensitive);
    text.replace(QDir::toNativeSeparators(QDir::homePath()), QStringLiteral("<USER>"), Qt::CaseInsensitive);
    static const QRegularExpression secrets(QStringLiteral(R"((?i)(bearer\s+|(?:token|password|memberKey|secret|authorization)\s*[:=]\s*)[^\s,;]+)"));
    text.replace(secrets, QStringLiteral("\\1<REDACTED>"));
    static const QRegularExpression boosty(QStringLiteral(R"((?i)\bBST(?:-?[A-HJ-NP-Z2-9]{8}){4}\b)"));
    text.replace(boosty, QStringLiteral("<BOOSTY_REDACTED>"));
    text.replace(QChar(0), QChar(0xfffd));
    return text.left(16384);
}
void DiagnosticLog::record(const QString& operation, const QString& code, const QString& message, const QString& details) {
    entries_.append(QStringLiteral("[%1] %2 / %3\n%4\n%5\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), sanitize(operation), sanitize(code), sanitize(message), sanitize(details)));
    while (entries_.size() > 100) entries_.removeFirst();
}
QString DiagnosticLog::snapshot() const {
    return QStringLiteral("RuTool 2 — диагностический журнал\nUTC: %1\nVersion: %2\nQt: %3\nOS: %4\nArchitecture: %5\n\n%6")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), QString::fromLatin1(RUTOOL_APP_VERSION), QString::fromLatin1(qVersion()),
             QSysInfo::productType(), QSysInfo::currentCpuArchitecture(), entries_.join(QLatin1Char('\n')));
}
bool DiagnosticLog::writeSnapshot(const QString& file, const QString& text, QString& error) {
    if (!file.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)) { error = QStringLiteral("Выберите файл с расширением .txt"); return false; }
    QSaveFile output(file);
    if (!output.open(QIODevice::WriteOnly)) { error = output.errorString(); return false; }
    const auto bytes = text.toUtf8();
    if (output.write(bytes) != bytes.size() || !output.commit()) { error = output.errorString(); return false; }
    return true;
}
