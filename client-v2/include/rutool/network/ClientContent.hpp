#pragma once
#include <rutool/core/RutProtocol.hpp>
#include <QByteArray>
#include <QVariantList>
namespace rutool {
QVariantList defaultCredits();
struct ClientContent {
    qint64 revision{};
    QString faq;
    QString license;
    QString licenseHash;
    QVariantList credits = defaultCredits();
};
Result<ClientContent> parseClientContent(const QByteArray& json);
}
