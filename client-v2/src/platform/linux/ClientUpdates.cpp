#include "ClientController.hpp"
#include <rutool/network/LinuxUpdates.hpp>
#include <rutool/platform/AppImageUpdates.hpp>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtConcurrent>

namespace {
QString repository() { return QStringLiteral(RUTOOL_LINUX_UPDATE_REPOSITORY); }
QString channelRef() { return QStringLiteral(RUTOOL_LINUX_UPDATE_CHANNEL_REF); }
QByteArray publicKey() {
    QFile file(QStringLiteral(":/linux-updates/public.der"));
    return file.open(QIODevice::ReadOnly) && file.size() <= 1024 ? file.readAll() : QByteArray{};
}
QString channelId(const QByteArray& key) {
    return QString::fromLatin1(QCryptographicHash::hash(repository().toUtf8() + '\n' + channelRef().toUtf8() + '\n' + key, QCryptographicHash::Sha256).toHex());
}
bool smoke() { return QCoreApplication::instance()->property("rutoolSmokeTest").toBool(); }
}
void ClientController::checkApplicationUpdate(bool candidate) {
    Q_UNUSED(candidate)
    if (smoke() || repository().isEmpty() || updateWatcher_.isRunning()) return;
    const auto key = publicKey(); const auto id = channelId(key); const auto cancel = lifetime_;
    const auto minimum = operationalSettings_.value(QStringLiteral("LinuxUpdateSequences")).toObject().value(id).toInteger();
    updateWatcher_.setFuture(QtConcurrent::run([key, id, cancel, minimum]() -> rutool::Result<UpdateInfo> {
        auto envelope = rutool::fetchLinuxChannel(repository(), channelRef(), cancel);
        if (!envelope) return std::unexpected(envelope.error());
        auto channel = rutool::verifyLinuxChannel(*envelope, key, repository(), QStringLiteral(RUTOOL_APP_VERSION), minimum);
        if (!channel) return std::unexpected(channel.error());
        UpdateInfo result; result.envelope = *envelope; result.linuxRelease = channel->next;
        result.channelSequence = channel->sequence; result.channelId = id;
        return result;
    }));
}
void ClientController::requestApplicationUpdate() {
    if (busy_) return;
    if (smoke()) operationStatus_ = QStringLiteral("Проверка обновлений отключена в режиме проверки интерфейса.");
    else if (repository().isEmpty()) operationStatus_ = QStringLiteral("Канал обновлений Linux не настроен в этой сборке.");
    else if (!linuxApplicationRelease_) {
        operationStatus_ = QStringLiteral("Проверяем обновления RuTool…"); checkApplicationUpdate();
    } else {
        auto current = rutool::platform::runningAppImage();
        if (!current) operationStatus_ = current.error().message;
        else if (applicationUpdateDestination_.isEmpty() && !QFileInfo(QFileInfo(*current).absolutePath()).isWritable()) {
            emit applicationUpdateLocationRequested(QUrl::fromLocalFile(QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).filePath(QStringLiteral("RuTool.AppImage"))));
        } else {
            askConfirmation(4, QStringLiteral("Обновление RuTool"),
                QStringLiteral("Скачать и установить версию %1? RuTool проверит подпись и файл, затем перезапустится. Если потребуется промежуточный выпуск, обновление продолжится автоматически.")
                .arg(linuxApplicationRelease_->version));
        }
    }
    emit changed();
}
void ClientController::runApplicationUpdate() {
    if (smoke() || !linuxApplicationRelease_) return;
    if (busy_) {
        if (QCoreApplication::instance()->property("rutoolUpdateHop").toInt() > 0)
            QTimer::singleShot(500, this, &ClientController::runApplicationUpdate);
        return;
    }
    auto current = rutool::platform::runningAppImage();
    if (!current) { operationStatus_ = current.error().message; emit changed(); return; }
    const int hop = QCoreApplication::instance()->property("rutoolUpdateHop").toInt();
    if (hop >= 8) { operationStatus_ = QStringLiteral("Цепочка обновлений слишком длинная."); emit changed(); return; }
    const auto release = *linuxApplicationRelease_;
    const auto destination = applicationUpdateDestination_.isEmpty() ? *current : applicationUpdateDestination_;
    const auto expectedHash = QCoreApplication::instance()->property("rutoolAppImageHash").toString();
    cancel_ = std::make_shared<std::atomic_bool>(false); const auto cancel = cancel_;
    busy_ = true; cancellable_ = true; operationStatus_ = QStringLiteral("Скачиваем и проверяем обновление RuTool…"); emit changed();
    workWatcher_.setFuture(QtConcurrent::run([release, original = *current, destination, expectedHash, cancel, hop]() -> rutool::Result<Work> {
        auto installed = rutool::platform::installAppImageUpdate(release, original, destination, expectedHash, cancel);
        if (!installed) return std::unexpected(installed.error());
        Work result; result.helper = *installed;
        result.helperArguments = {QStringLiteral("--continue-update"), QString::number(hop + 1)};
        result.status = QStringLiteral("Обновление проверено. Перезапускаем RuTool…");
        return result;
    }));
}
