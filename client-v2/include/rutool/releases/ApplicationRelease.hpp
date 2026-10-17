#pragma once
#include <rutool/core/RutProtocol.hpp>
#include <QVector>
namespace rutool {
struct ApplicationFile { QString path; qint64 bytes{}; QString sha256, asset; };
struct ApplicationRelease { qint64 sequence{}; QString version, repository, tag; QVector<ApplicationFile> files; };
bool allowedApplicationPath(const QString& path);
Result<ApplicationRelease> verifyApplicationEnvelope(const QByteArray& envelope, const QByteArray& pinnedSpki);
}
