#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QHash>
#include <QPointer>
#include <memory>
#include <rutool/releases/ApplicationRelease.hpp>

namespace rutool {
class ServiceClient final : public QObject {
    Q_OBJECT
public:
    explicit ServiceClient(QByteArray updatePublicKey, QObject* parent = nullptr);
    ServiceClient(QByteArray updatePublicKey, std::unique_ptr<QNetworkAccessManager> transport, QObject* parent = nullptr);
    void checkHealth();
    void checkApplicationRelease();
    void checkClientContent();
    void cancel();
    static bool allowedEndpoint(const QUrl& url);
signals:
    void diagnosticError(const QString& operation, const QString& code, const QString& message, const QString& details);
    void healthChecked(bool ok, const QString& detail);
    void releaseChecked(bool ok, const QString& version, const QString& detail);
    void contentChecked(bool ok, const QByteArray& json, const QString& detail);
private:
    enum class Operation { Health, Release, Content };
    void request(Operation operation);
    std::unique_ptr<QNetworkAccessManager> manager_;
    QByteArray updatePublicKey_;
    QHash<int, QPointer<QNetworkReply>> pending_;
};
}
