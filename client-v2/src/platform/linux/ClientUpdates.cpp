#include "ClientController.hpp"

void ClientController::checkApplicationUpdate(bool) {
    // Windows release metadata is not a Linux update channel.
}
void ClientController::requestApplicationUpdate() {
    operationStatus_ = QStringLiteral("Для обновления Linux-версии замените AppImage вручную.");
    emit changed();
}
void ClientController::runApplicationUpdate() { requestApplicationUpdate(); }
