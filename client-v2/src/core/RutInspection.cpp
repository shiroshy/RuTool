#include <rutool/core/RutInspection.hpp>
#include <QFile>
#include <QCryptographicHash>
#include <QtEndian>
#include <limits>

namespace rutool {
Result<RutInspection> inspectRut(const QString& path, const std::shared_ptr<std::atomic_bool>& cancel) {
    QFile input(path);
    const auto fail = [&input](ErrorCode code, const QString& message, const QString& stage, const QString& extra = QString{}) -> Result<RutInspection> {
        return std::unexpected(Error{code, message, QStringLiteral("Stage: %1\nOffset: %2\nFileBytes: %3\n%4")
            .arg(stage).arg(input.pos()).arg(input.size()).arg(extra)});
    };
    if (!input.open(QIODevice::ReadOnly))
        return fail(ErrorCode::Io, QStringLiteral("Не удалось открыть файл .rut"), QStringLiteral("open"), input.errorString());
    const qint64 originalSize = input.size();
    const auto header = input.read(32);
    if (header.size() != 32)
        return fail(ErrorCode::Truncated, QStringLiteral("Заголовок .rut обрезан"), QStringLiteral("header"), QStringLiteral("Expected: 32; actual: %1").arg(header.size()));
    if (header.first(8) != QByteArray("RUT111\0\0", 8))
        return fail(ErrorCode::UnsupportedFormat, QStringLiteral("Поддерживается контейнер RUT111"), QStringLiteral("magic"));
    QCryptographicHash hash(QCryptographicHash::Sha256); hash.addData(header);
    RutInspection info{static_cast<quint64>(originalSize), 0, 0, {}};
    while (input.pos() < originalSize) {
        if (cancel && cancel->load())
            return fail(ErrorCode::Cancelled, QStringLiteral("Проверка отменена"), QStringLiteral("chunk"));
        const auto word = input.read(4);
        if (word.size() != 4)
            return fail(ErrorCode::Truncated, QStringLiteral("Обрезан размер блока"), QStringLiteral("chunk-length"), QStringLiteral("Chunk: %1; expected: 4; actual: %2").arg(info.chunks).arg(word.size()));
        const quint32 length = qFromLittleEndian<quint32>(word.constData());
        if (length == 0 || length > 1024U * 1024U)
            return fail(ErrorCode::InvalidInput, QStringLiteral("Повреждён размер блока .rut"), QStringLiteral("chunk-length"), QStringLiteral("Chunk: %1; length: %2; limit: 1048576").arg(info.chunks).arg(length));
        if (info.chunks >= std::numeric_limits<quint32>::max())
            return fail(ErrorCode::InvalidInput, QStringLiteral("Переполнение номера блока"), QStringLiteral("chunk-index"));
        const auto body = input.read(static_cast<qint64>(length) + 16);
        if (body.size() != static_cast<qint64>(length) + 16)
            return fail(ErrorCode::Truncated, QStringLiteral("Блок .rut обрезан"), QStringLiteral("chunk-body"), QStringLiteral("Chunk: %1; expected: %2; actual: %3").arg(info.chunks).arg(static_cast<quint64>(length) + 16).arg(body.size()));
        hash.addData(word); hash.addData(body);
        ++info.chunks; info.ciphertextBytes += length;
    }
    if (info.chunks == 0 || input.size() != originalSize || input.error() != QFileDevice::NoError)
        return fail(ErrorCode::InvalidInput, QStringLiteral("Контейнер пуст или изменился при чтении"), QStringLiteral("final-check"), QStringLiteral("Chunks: %1; original bytes: %2").arg(info.chunks).arg(originalSize));
    info.sha256 = QString::fromLatin1(hash.result().toHex());
    return info;
}
}
