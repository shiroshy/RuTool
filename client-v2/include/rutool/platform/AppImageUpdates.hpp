#pragma once
#include <rutool/network/Transfers.hpp>
#include <rutool/releases/LinuxRelease.hpp>

namespace rutool::platform {
Result<QString> runningAppImage();
// The caller must obtain release from verified channel metadata.
// destination may be current or a new, unused filename in a writable directory.
// expectedCurrentHash binds the operation to the AppImage seen at startup.
Result<QString> installAppImageUpdate(const LinuxRelease& release, const QString& current,
    const QString& destination, const QString& expectedCurrentHash, const Cancellation& cancel, const TransferProgress& progress = {},
    QNetworkAccessManager* transport = nullptr);
}
