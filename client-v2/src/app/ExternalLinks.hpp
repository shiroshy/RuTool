#pragma once
#include <QObject>
#include <QProcessEnvironment>
#include <QUrl>

class ExternalLinks final : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    void open(const QUrl& url);
    static QProcessEnvironment desktopEnvironment(QProcessEnvironment environment);
signals:
    void failed(const QString& detail);
};
