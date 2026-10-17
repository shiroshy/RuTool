#pragma once
#include <rutool/core/Package.hpp>
#include <QJsonObject>
#include <QNetworkAccessManager>

namespace rutool {
class InstallerApi final {
public:
    explicit InstallerApi(Cancellation cancel = {});
    Result<QJsonObject> get(const QString& endpoint);
    Result<QJsonObject> post(const QString& endpoint, const QJsonObject& object);
    Result<bool> verifyPackage(const PreparedPackage& package);
private:
    Result<QJsonObject> json(const QString& endpoint, const QByteArray& body, bool post);
    QNetworkAccessManager manager_;
    Cancellation cancel_;
};
QJsonValue apiValue(const QJsonObject& object, const QString& name);
}
