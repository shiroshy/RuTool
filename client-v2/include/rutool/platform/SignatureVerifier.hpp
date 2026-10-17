#pragma once
#include <QByteArray>
namespace rutool::platform {
bool verifyP256(const QByteArray& message, const QByteArray& signature, const QByteArray& spki);
}
