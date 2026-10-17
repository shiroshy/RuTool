#pragma once
#include <QString>
#include <QStringList>

class DiagnosticLog {
public:
    void record(const QString& operation, const QString& code, const QString& message, const QString& details = {});
    QString snapshot() const;
    static QString sanitize(QString text);
    static bool writeSnapshot(const QString& file, const QString& text, QString& error);
private:
    QStringList entries_;
};
