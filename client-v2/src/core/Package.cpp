#include <rutool/core/Package.hpp>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <limits>
#include <zlib.h>
#include <rutool/platform/FileOps.hpp>

namespace rutool {
namespace {
Error bad(const QString& text) { return {ErrorCode::InvalidInput, text}; }
bool cancelled(const Cancellation& cancel) { return cancel && cancel->load(); }
quint16 u16(const QByteArray& bytes, qsizetype offset) { return qFromLittleEndian<quint16>(bytes.constData() + offset); }
quint32 u32(const QByteArray& bytes, qsizetype offset) { return qFromLittleEndian<quint32>(bytes.constData() + offset); }
Result<QJsonObject> jsonFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 256 * 1024) return std::unexpected(bad(QStringLiteral("Описание пакета недоступно или слишком велико.")));
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return std::unexpected(bad(QStringLiteral("Описание пакета повреждено.")));
    return doc.object();
}
struct ZipEntry { QByteArray name; quint16 flags{}; quint16 method{}; quint32 crc{}; quint32 packed{}; quint32 plain{}; quint32 offset{}; qint64 dataOffset{}; qint64 end{}; };
struct Inflate { z_stream stream{}; bool ready{}; ~Inflate() { if (ready) inflateEnd(&stream); } };
}
Result<QString> hashFile(const QString& path, const Cancellation& cancel) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || !QFileInfo(file).isFile()) return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Не удалось прочитать файл."), file.errorString()});
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        if (cancelled(cancel)) return std::unexpected(Error{ErrorCode::Cancelled, QStringLiteral("Операция отменена.")});
        const auto buffer = file.read(1024 * 1024);
        if (buffer.isEmpty() && file.error() != QFileDevice::NoError) return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Ошибка чтения файла."), file.errorString()});
        hash.addData(buffer);
    }
    return QString::fromLatin1(hash.result().toHex());
}
Result<void> extractPackageZip(const QString& path, const QString& folder, const Cancellation& cancel) {
    QDir destination(folder);
    if (!destination.exists() || !destination.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty() || QFileInfo(folder).isSymLink())
        return std::unexpected(bad(QStringLiteral("Для распаковки нужен пустой временный каталог.")));
    QFile zip(path);
    if (!zip.open(QIODevice::ReadOnly) || zip.size() < 22 || zip.size() > 3LL * 1024 * 1024 * 1024)
        return std::unexpected(bad(QStringLiteral("Некорректный размер ZIP пакета.")));
    const auto tailSize = std::min<qint64>(zip.size(), 65557);
    if (!zip.seek(zip.size() - tailSize)) return std::unexpected(bad(QStringLiteral("Ошибка чтения ZIP.")));
    const auto tail = zip.read(tailSize);
    qsizetype eocd = -1;
    for (auto i = tail.size() - 22; i >= 0; --i) {
        if (u32(tail, i) == 0x06054b50U && i + 22 + u16(tail, i + 20) == tail.size()) { eocd = i; break; }
    }
    if (eocd < 0 || u16(tail, eocd + 4) || u16(tail, eocd + 6) || u16(tail, eocd + 8) != u16(tail, eocd + 10))
        return std::unexpected(bad(QStringLiteral("Повреждённый или многотомный ZIP.")));
    const auto count = u16(tail, eocd + 10);
    const auto centralSize = u32(tail, eocd + 12), centralOffset = u32(tail, eocd + 16);
    const auto eocdOffset = zip.size() - tailSize + eocd;
    if ((count != 2 && count != 3 && count != 6) || centralSize > 65536 || static_cast<qint64>(centralOffset) + centralSize != eocdOffset || !zip.seek(centralOffset))
        return std::unexpected(bad(QStringLiteral("Неизвестный состав ZIP пакета.")));
    const auto central = zip.read(centralSize);
    if (central.size() != centralSize) return std::unexpected(bad(QStringLiteral("Обрезанный каталог ZIP.")));
    std::vector<ZipEntry> entries; QSet<QByteArray> names; qsizetype position{}; quint64 total{};
    for (int i = 0; i < count; ++i) {
        if (position + 46 > central.size() || u32(central, position) != 0x02014b50U)
            return std::unexpected(bad(QStringLiteral("Повреждённый каталог ZIP.")));
        const auto nameSize = u16(central, position + 28), extraSize = u16(central, position + 30), commentSize = u16(central, position + 32);
        const auto next = position + 46 + nameSize + extraSize + commentSize;
        if (next > central.size() || u16(central, position + 34)) return std::unexpected(bad(QStringLiteral("Повреждённая запись ZIP.")));
        ZipEntry entry{central.mid(position + 46, nameSize), u16(central, position + 8), u16(central, position + 10),
            u32(central, position + 16), u32(central, position + 20), u32(central, position + 24), u32(central, position + 42)};
        const auto attributes = u32(central, position + 38);
        if ((entry.flags & ~quint16{0x0808}) || (entry.method != 0 && entry.method != 8) || entry.plain > 1024U * 1024 * 1024 ||
            entry.packed == std::numeric_limits<quint32>::max() || entry.offset >= centralOffset || names.contains(entry.name) ||
            ((attributes >> 16) & 0170000U) == 0120000U)
            return std::unexpected(bad(QStringLiteral("Неподдерживаемая или небезопасная запись ZIP.")));
        total += entry.plain;
        if (total > 3ULL * 1024 * 1024 * 1024) return std::unexpected(bad(QStringLiteral("Распакованный пакет слишком велик.")));
        names.insert(entry.name); entries.push_back(std::move(entry)); position = next;
    }
    const QSet<QByteArray> basic{"ExcelDB.db", "ExcelDB.db.baretool.json"};
    auto signedNames = basic; signedNames.insert("signature.json");
    auto bundleNames = signedNames; bundleNames.unite(QSet<QByteArray>{"Excel.zip", "resources.assets", "bundle.json"});
    if (position != central.size() || (names != basic && names != signedNames && names != bundleNames))
        return std::unexpected(bad(QStringLiteral("Неизвестный состав пакета .rut.")));
    std::ranges::sort(entries, {}, &ZipEntry::offset);
    qint64 previousEnd{};
    for (auto& entry : entries) {
        if (entry.offset != previousEnd || !zip.seek(entry.offset)) return std::unexpected(bad(QStringLiteral("Перекрывающиеся или неполные записи ZIP.")));
        const auto local = zip.read(30);
        if (local.size() != 30 || u32(local, 0) != 0x04034b50U || u16(local, 6) != entry.flags || u16(local, 8) != entry.method || u16(local, 26) != entry.name.size())
            return std::unexpected(bad(QStringLiteral("Заголовки ZIP не соответствуют каталогу.")));
        if (!(entry.flags & 8) && (u32(local, 14) != entry.crc || u32(local, 18) != entry.packed || u32(local, 22) != entry.plain))
            return std::unexpected(bad(QStringLiteral("Размеры ZIP не соответствуют каталогу.")));
        if (zip.read(entry.name.size()) != entry.name) return std::unexpected(bad(QStringLiteral("Имя файла ZIP подменено.")));
        entry.dataOffset = static_cast<qint64>(entry.offset) + 30 + entry.name.size() + u16(local, 28);
        entry.end = entry.dataOffset + entry.packed;
        if (entry.end > centralOffset || !zip.seek(entry.end)) return std::unexpected(bad(QStringLiteral("Обрезанная запись ZIP.")));
        if (entry.flags & 8) {
            auto descriptor = zip.read(4);
            if (descriptor.size() != 4) return std::unexpected(bad(QStringLiteral("Обрезанный дескриптор ZIP.")));
            if (u32(descriptor, 0) == 0x08074b50U) { descriptor = zip.read(12); entry.end += 16; }
            else { descriptor += zip.read(8); entry.end += 12; }
            if (descriptor.size() != 12 || u32(descriptor, 0) != entry.crc || u32(descriptor, 4) != entry.packed || u32(descriptor, 8) != entry.plain)
                return std::unexpected(bad(QStringLiteral("Неверный дескриптор ZIP.")));
        }
        previousEnd = entry.end;
    }
    if (previousEnd != centralOffset) return std::unexpected(bad(QStringLiteral("Повреждённые границы ZIP.")));
    auto space = platform::requireFreeSpace({{folder, total}}); if (!space) return std::unexpected(space.error());
    for (const auto& entry : entries) {
        if (cancelled(cancel)) return std::unexpected(Error{ErrorCode::Cancelled, QStringLiteral("Распаковка отменена.")});
        QFile output(destination.filePath(QString::fromLatin1(entry.name)));
        if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly) || !zip.seek(entry.dataOffset))
            return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Не удалось создать временный файл."), output.errorString()});
        Inflate state;
        if (entry.method == 8) {
            if (inflateInit2(&state.stream, -15) != Z_OK) return std::unexpected(bad(QStringLiteral("Распаковщик ZIP недоступен.")));
            state.ready = true;
        }
        quint64 produced{}; quint32 remaining = entry.packed; auto checksum = crc32(0, nullptr, 0); bool ended = entry.method == 0;
        while (remaining) {
            if (cancelled(cancel)) return std::unexpected(Error{ErrorCode::Cancelled, QStringLiteral("Распаковка отменена.")});
            auto input = zip.read(std::min<quint32>(remaining, 65536));
            if (input.isEmpty()) return std::unexpected(bad(QStringLiteral("Обрезанные данные ZIP.")));
            remaining -= static_cast<quint32>(input.size());
            if (entry.method == 0) {
                produced += static_cast<quint64>(input.size());
                if (produced > entry.plain || output.write(input) != input.size()) return std::unexpected(bad(QStringLiteral("Ошибка записи ZIP.")));
                checksum = crc32(checksum, reinterpret_cast<const Bytef*>(input.constData()), static_cast<uInt>(input.size()));
            } else {
                if (ended) return std::unexpected(bad(QStringLiteral("Лишние сжатые данные ZIP.")));
                state.stream.next_in = reinterpret_cast<Bytef*>(input.data()); state.stream.avail_in = static_cast<uInt>(input.size());
                do {
                    QByteArray buffer(65536, '\0'); state.stream.next_out = reinterpret_cast<Bytef*>(buffer.data()); state.stream.avail_out = 65536;
                    const auto before = state.stream.avail_in; const int status = inflate(&state.stream, Z_NO_FLUSH);
                    const auto written = 65536U - state.stream.avail_out; produced += written;
                    if ((status != Z_OK && status != Z_STREAM_END) || produced > entry.plain || (before == state.stream.avail_in && !written) || output.write(buffer.constData(), written) != written)
                        return std::unexpected(bad(QStringLiteral("Повреждённые сжатые данные ZIP.")));
                    checksum = crc32(checksum, reinterpret_cast<const Bytef*>(buffer.constData()), written);
                    if (status == Z_STREAM_END) { ended = true; break; }
                } while (state.stream.avail_in || state.stream.avail_out == 0);
                if (ended && (state.stream.avail_in || remaining)) return std::unexpected(bad(QStringLiteral("Лишние сжатые данные ZIP.")));
            }
        }
        if (!ended || produced != entry.plain || checksum != entry.crc || !output.flush())
            return std::unexpected(bad(QStringLiteral("Размер или CRC файла ZIP не совпадает.")));
    }
    return {};
}
Result<PreparedPackage> validatePreparedPackage(const QString& folder, const Cancellation& cancel) {
    const QDir directory(folder);
    auto manifest = jsonFile(directory.filePath(QStringLiteral("ExcelDB.db.baretool.json")));
    if (!manifest) return std::unexpected(manifest.error());
    PreparedPackage result; result.folder = folder;
    result.originalHash = manifest->value(QStringLiteral("OriginalSha256")).toString();
    result.patchedHash = manifest->value(QStringLiteral("PatchedSha256")).toString();
    result.watermarkHash = manifest->value(QStringLiteral("WatermarkSha256")).toString();
    const auto changed = manifest->value(QStringLiteral("ChangedRows"));
    if (manifest->value(QStringLiteral("FormatVersion")).toInt() != 1 || !isHash(result.originalHash) || !isHash(result.patchedHash) ||
        !changed.isDouble() || changed.toDouble() < 0 || changed.toDouble() != changed.toInt(-1))
        return std::unexpected(bad(QStringLiteral("Некорректное описание базы .rut.")));
    auto actual = hashFile(directory.filePath(QStringLiteral("ExcelDB.db")), cancel);
    if (!actual) return std::unexpected(actual.error());
    if (*actual != result.patchedHash) return std::unexpected(bad(QStringLiteral("Хеш базы .rut не совпадает с описанием.")));
    result.bundle = QFileInfo::exists(directory.filePath(QStringLiteral("bundle.json")));
    QString signedHash = result.patchedHash;
    if (result.bundle) {
        auto bundle = jsonFile(directory.filePath(QStringLiteral("bundle.json")));
        if (!bundle) return std::unexpected(bundle.error());
        const std::array<QString, 5> fields{QStringLiteral("DatabaseSha256"), QStringLiteral("ExcelZipSha256"), QStringLiteral("ResourcesSha256"), QStringLiteral("OriginalExcelZipSha256"), QStringLiteral("OriginalResourcesSha256")};
        for (size_t i = 0; i < fields.size(); ++i) result.bundleHashes[i] = bundle->value(fields[i]).toString();
        auto combined = bundleHash(bundle->value(QStringLiteral("FormatVersion")).toInt(), result.bundleHashes);
        if (!combined || result.bundleHashes[0] != result.patchedHash) return std::unexpected(bad(QStringLiteral("Некорректное описание общего пакета.")));
        for (const auto& pair : {std::pair{QStringLiteral("Excel.zip"), result.bundleHashes[1]}, std::pair{QStringLiteral("resources.assets"), result.bundleHashes[2]}}) {
            const auto hash = hashFile(directory.filePath(pair.first), cancel);
            if (!hash) return std::unexpected(hash.error());
            if (*hash != pair.second) return std::unexpected(bad(QStringLiteral("Дополнительные файлы .rut повреждены.")));
        }
        signedHash = *combined;
    }
    if (QFileInfo::exists(directory.filePath(QStringLiteral("signature.json")))) {
        auto signature = jsonFile(directory.filePath(QStringLiteral("signature.json")));
        if (!signature) return std::unexpected(signature.error());
        const auto message = signatureMessage(signature->value(QStringLiteral("FileSha256")).toString(), signature->value(QStringLiteral("RuToolVersion")).toString(),
            signature->value(QStringLiteral("WatermarkSha256")).toString(), signature->value(QStringLiteral("SignerId")).toString());
        if (!message || signature->value(QStringLiteral("FormatVersion")).toInt() != 1 || signature->value(QStringLiteral("FileSha256")).toString() != signedHash ||
            signature->value(QStringLiteral("WatermarkSha256")).toString() != result.watermarkHash)
            return std::unexpected(bad(QStringLiteral("Файлы перевода не соответствуют подписи.")));
        result.signature = *signature;
    }
    return result;
}
Result<PreparedPackage> decryptPackage(const QString& source, const QString& folder, const platform::Secret& password, const Cancellation& cancel) {
    auto sourceLock = platform::FileLock::read(source); if (!sourceLock) return std::unexpected(sourceLock.error());
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Пакет недоступен.")});
    const auto header = input.read(32);
    if (header.size() != 32 || header.left(8) != QByteArray("RUT111\0\0", 8)) return std::unexpected(bad(QStringLiteral("Неизвестный или устаревший формат .rut.")));
    auto key = platform::deriveRutKey(password, header.mid(8, 16));
    if (!key) return std::unexpected(key.error());
    const auto zipPath = folder + QStringLiteral(".decrypted.zip");
    auto space = platform::requireFreeSpace({{zipPath, static_cast<quint64>(input.size())}}); if (!space) return std::unexpected(space.error());
    QCryptographicHash sourceHash(QCryptographicHash::Sha256); sourceHash.addData(header);
    struct Cleanup { QString path; ~Cleanup() { QFile::remove(path); } } cleanup{zipPath};
    QFile output(zipPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Временный ZIP недоступен.")});
    quint64 index{}, total{};
    while (!input.atEnd()) {
        if (cancelled(cancel)) return std::unexpected(Error{ErrorCode::Cancelled, QStringLiteral("Чтение .rut отменено.")});
        const auto word = input.read(4);
        if (word.size() != 4 || index > std::numeric_limits<quint32>::max()) return std::unexpected(bad(QStringLiteral("Обрезанный или слишком большой .rut.")));
        const auto count = u32(word, 0);
        if (!count || count > 1024 * 1024) return std::unexpected(bad(QStringLiteral("Некорректный размер блока .rut.")));
        const auto cipher = input.read(count), tag = input.read(16);
        if (cipher.size() != count || tag.size() != 16) return std::unexpected(bad(QStringLiteral("Обрезанный блок .rut.")));
        sourceHash.addData(word); sourceHash.addData(cipher); sourceHash.addData(tag);
        total += count;
        if (total > 3ULL * 1024 * 1024 * 1024) return std::unexpected(bad(QStringLiteral("Пакет слишком велик.")));
        auto plain = platform::decryptGcm(*key, chunkNonce(header.mid(24, 8), static_cast<quint32>(index)), cipher, tag, chunkAad(static_cast<quint32>(index)));
        if (!plain) return std::unexpected(plain.error());
        if (output.write(plain->bytes()) != count) return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Не удалось записать распакованный блок.")});
        ++index;
    }
    if (!output.flush()) return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Не удалось записать ZIP пакета.")});
    output.close(); input.close();
    auto extracted = extractPackageZip(zipPath, folder, cancel);
    if (!extracted) return std::unexpected(extracted.error());
    auto prepared = validatePreparedPackage(folder, cancel);
    if (!prepared) return std::unexpected(prepared.error());
    if (cancelled(cancel)) return std::unexpected(Error{ErrorCode::Cancelled, QStringLiteral("Чтение .rut отменено.")});
    prepared->source = source; prepared->sourceHash = QString::fromLatin1(sourceHash.result().toHex());
    return prepared;
}
}
