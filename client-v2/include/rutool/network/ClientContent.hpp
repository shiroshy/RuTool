#pragma once
#include <rutool/core/RutProtocol.hpp>
#include <QByteArray>
namespace rutool {
struct ClientContent {
    qint64 revision{};
    QString faq;
    QString license;
    QString licenseHash;
};
Result<ClientContent> parseClientContent(const QByteArray& json);
}
