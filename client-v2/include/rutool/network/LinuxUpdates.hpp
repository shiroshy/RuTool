#pragma once
#include <rutool/core/Package.hpp>
#include <rutool/releases/LinuxRelease.hpp>
#include <QNetworkAccessManager>

namespace rutool {
Result<QByteArray> fetchLinuxChannel(const QString& repository, const QString& channelRef,
    const Cancellation& cancel, QNetworkAccessManager* transport = nullptr);
}
