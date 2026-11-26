#pragma once
#include <rutool/core/RutProtocol.hpp>
#include <QUrl>
#include <optional>

namespace rutool {
struct LinuxRelease {
    QString version, sha256;
    qint64 bytes{};
    QUrl url;
};
struct LinuxChannel {
    qint64 sequence{};
    std::optional<LinuxRelease> next;
};
bool validGithubRepository(const QString& repository);
Result<LinuxChannel> verifyLinuxChannel(const QByteArray& envelope, const QByteArray& publicKey,
    const QString& repository, const QString& currentVersion, qint64 minimumSequence);
}
