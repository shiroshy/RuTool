#include <rutool/core/Installation.hpp>
#include <rutool/platform/FileOps.hpp>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>
#include <QRegularExpression>

namespace rutool {
namespace {
Error invalid(const QString& text) { return {ErrorCode::InvalidInput, text}; }
QString full(const QString& path) { return QDir::cleanPath(QFileInfo(path).absoluteFilePath()); }
bool samePath(const QString& a, const QString& b) { return full(a).compare(full(b), Qt::CaseInsensitive) == 0; }
std::vector<QString> targets(const QString& game, bool bundle) {
    std::vector<QString> result{gameDatabasePath(game)};
    if (bundle) { result.push_back(QFileInfo(result[0]).dir().filePath(QStringLiteral("Excel.zip"))); result.push_back(QDir(game).filePath(QStringLiteral("BlueArchive_Data/resources.assets"))); }
    return result;
}
Result<void> sidecars(const QString& path) {
    for (const auto& suffix : {QStringLiteral("-wal"), QStringLiteral("-shm"), QStringLiteral("-journal")})
        if (QFileInfo::exists(path + suffix)) return std::unexpected(invalid(QStringLiteral("Закройте программу, использующую базу: рядом найдены WAL/SHM/journal.")));
    return {};
}
Result<QJsonObject> readObject(const QString& path) {
    auto safe = platform::requireSafeFile(path);
    if (!safe) return std::unexpected(safe.error());
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024) return std::unexpected(invalid(QStringLiteral("Описание оригинальной копии недоступно.")));
    QJsonParseError error; const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return std::unexpected(invalid(QStringLiteral("Описание оригинальной копии повреждено.")));
    return doc.object();
}
Result<void> writeObject(const QString& path, const QJsonObject& object) {
    QSaveFile output(path); const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
        return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Не удалось сохранить описание оригинальной копии."), output.errorString()});
    return {};
}
Result<void> exactHash(const QString& path, const QString& expected, const Cancellation& cancel) {
    auto actual = hashFile(path, cancel);
    if (!actual) return std::unexpected(actual.error());
    if (*actual != expected) return std::unexpected(invalid(QStringLiteral("Файл изменился или его хеш не совпадает: %1").arg(QFileInfo(path).fileName())));
    return {};
}
QJsonObject databaseReceipt(const FileReplacement& file, bool verified) {
    return {{QStringLiteral("FormatVersion"), 1}, {QStringLiteral("BeforeSha256"), file.before}, {QStringLiteral("AfterSha256"), file.after},
        {QStringLiteral("Forced"), !verified}, {QStringLiteral("TargetPath"), full(file.target)}, {QStringLiteral("OriginalVerified"), verified}};
}
QJsonObject bundleReceipt(const std::vector<FileReplacement>& files) {
    QJsonArray items;
    for (const auto& file : files) items.append(QJsonObject{{QStringLiteral("Target"), full(file.target)}, {QStringLiteral("Backup"), full(file.backup)},
        {QStringLiteral("BeforeSha256"), file.before}, {QStringLiteral("AfterSha256"), file.after}});
    return {{QStringLiteral("FormatVersion"), 1}, {QStringLiteral("Items"), items}, {QStringLiteral("OriginalVerified"), true}};
}
QString journalPath(const QString& database) { return database + QStringLiteral(".baretool-transaction.dpapi"); }
Result<void> writeJournal(const QString& database, const QJsonObject& object) {
    auto encrypted = platform::protectUser(QJsonDocument(object).toJson(QJsonDocument::Compact), full(database).toUtf8());
    if (!encrypted) return std::unexpected(encrypted.error());
    QSaveFile file(journalPath(database));
    if (!file.open(QIODevice::WriteOnly) || file.write(*encrypted) != encrypted->size() || !file.commit()) return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Не удалось сохранить журнал восстановления.")});
    return platform::flushFile(journalPath(database));
}
Result<void> restoreReceipts(const QJsonArray& receipts) {
    for (const auto& value : receipts) {
        const auto item = value.toObject(); auto saved = writeObject(item.value(QStringLiteral("Path")).toString(), item.value(QStringLiteral("Object")).toObject());
        if (!saved) return std::unexpected(saved.error());
    }
    return {};
}
Result<QString> replace(std::vector<FileReplacement> files, const QString& backupRoot, bool retainOriginal, const CommitGuard& guard, const Cancellation& cancel, const CommitGuard& finalize = {}, const QJsonArray& oldReceipts = {}, const QString& keptBackup = {}) {
    if (files.empty() || !guard) return std::unexpected(invalid(QStringLiteral("Не задана проверка перед записью.")));
    auto closed = platform::requireGameClosed();
        if (!closed) return std::unexpected(closed.error());
    auto operation = platform::FileLock::operation(files[0].target);
        if (!operation) return std::unexpected(operation.error());
    if (QFileInfo::exists(journalPath(files[0].target))) return std::unexpected(invalid(QStringLiteral("Сначала требуется завершить восстановление прерванной операции.")));
    std::vector<std::unique_ptr<platform::FileLock>> locks;
    for (const auto& item : files) {
        if (samePath(item.source, item.target) || !isHash(item.before) || !isHash(item.after)) return std::unexpected(invalid(QStringLiteral("Некорректный источник установки.")));
        for (const auto& path : {item.source, item.target}) { auto checked = sidecars(path);
        if (!checked) return std::unexpected(checked.error()); }
        auto source = platform::FileLock::read(item.source), target = platform::FileLock::read(item.target, true);
        if (!source) return std::unexpected(source.error());
        if (!target) return std::unexpected(target.error());
        locks.push_back(std::move(*source)); locks.push_back(std::move(*target));
        auto sourceHash = exactHash(item.source, item.after, cancel), targetHash = exactHash(item.target, item.before, cancel);
        if (!sourceHash) return std::unexpected(sourceHash.error());
        if (!targetHash) return std::unexpected(targetHash.error());
    }
    if (retainOriginal && !QDir().mkpath(backupRoot)) return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Не удалось создать каталог резервных копий.")});
    const auto id = QUuid::createUuid().toString(QUuid::Id128);
    std::vector<QString> staged;
    std::vector<QString> backups;
    bool committed{}, preserveRecovery{};
    struct Cleanup {
        std::vector<QString>& staged; std::vector<QString>& backups; bool& committed; bool& preserveRecovery; bool retain;
        ~Cleanup() {
            for (const auto& file : staged) QFile::remove(file);
            if (!preserveRecovery && (!committed || !retain)) {
                for (const auto& file : backups) QFile::remove(file);
                if (!backups.empty()) { QFile::remove(backups[0] + QStringLiteral(".baretool-backup.json")); QFile::remove(backups[0] + QStringLiteral(".rut-bundle.json")); }
            }
        }
    } cleanup{staged, backups, committed, preserveRecovery, retainOriginal};
    for (auto& item : files) {
        const auto stage = item.target + QStringLiteral(".baretool-install-") + id + QStringLiteral(".tmp");
        item.backup = retainOriginal ? QDir(backupRoot).filePath(QFileInfo(item.target).fileName() + QStringLiteral(".baretool-backup-") + id + QStringLiteral(".db"))
            : item.target + QStringLiteral(".baretool-rollback-") + id + QStringLiteral(".tmp");
        if (QFileInfo::exists(stage) || QFileInfo::exists(item.backup)) return std::unexpected(invalid(QStringLiteral("Временный файл уже существует.")));
        staged.push_back(stage); backups.push_back(item.backup);
        if (!QFile::copy(item.source, stage) || !QFile::copy(item.target, item.backup)) return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Не удалось подготовить замену и резервную копию.")});
        auto stageHash = exactHash(stage, item.after, cancel), backupHash = exactHash(item.backup, item.before, cancel);
        if (!stageHash) return std::unexpected(stageHash.error());
        if (!backupHash) return std::unexpected(backupHash.error());
        auto flushStage = platform::flushFile(stage), flushBackup = platform::flushFile(item.backup);
        if (!flushStage) return std::unexpected(flushStage.error());
        if (!flushBackup) return std::unexpected(flushBackup.error());
    }
    closed = platform::requireGameClosed();
        if (!closed) return std::unexpected(closed.error());
    for (const auto& item : files) {
        auto valid = sidecars(item.target);
        if (!valid) return std::unexpected(valid.error());
        auto hash = exactHash(item.target, item.before, cancel);
        if (!hash) return std::unexpected(hash.error());
    }
    auto permitted = guard();
        if (!permitted) return std::unexpected(permitted.error());
    if (cancel && cancel->load()) return std::unexpected(Error{ErrorCode::Cancelled, QStringLiteral("Установка отменена до записи.")});
    if (retainOriginal) {
        auto receipt = writeObject(backups[0] + QStringLiteral(".baretool-backup.json"), databaseReceipt(files[0], true));
        if (!receipt) return std::unexpected(receipt.error());
        if (files.size() == 3) { receipt = writeObject(backups[0] + QStringLiteral(".rut-bundle.json"), bundleReceipt(files));
        if (!receipt) return std::unexpected(receipt.error()); }
    }
    QJsonArray records;
    for (size_t i = 0; i < files.size(); ++i) records.append(QJsonObject{{QStringLiteral("Target"), full(files[i].target)}, {QStringLiteral("Backup"), full(backups[i])}, {QStringLiteral("Stage"), full(staged[i])}, {QStringLiteral("Before"), files[i].before}, {QStringLiteral("After"), files[i].after}});
    QJsonObject journal{{QStringLiteral("FormatVersion"), 1}, {QStringLiteral("Id"), id}, {QStringLiteral("Files"), records}, {QStringLiteral("OriginalRetained"), retainOriginal},
        {QStringLiteral("OriginalBackup"), retainOriginal ? backups[0] : keptBackup}, {QStringLiteral("OldReceipts"), oldReceipts}, {QStringLiteral("Committed"), false}};
    auto recorded = writeJournal(files[0].target, journal); if (!recorded) { preserveRecovery = QFileInfo::exists(journalPath(files[0].target)); return std::unexpected(recorded.error()); }
    size_t installed{};
    auto commit = [&]() -> Result<void> {
        for (size_t i = 0; i < files.size(); ++i) {
            auto current = exactHash(files[i].target, files[i].before, {});
        if (!current) return std::unexpected(current.error());
            auto changed = platform::replaceFile(staged[i], files[i].target);
        if (!changed) return std::unexpected(changed.error());
            ++installed;
            auto after = exactHash(files[i].target, files[i].after, {});
        if (!after) return std::unexpected(after.error());
        }
        if (finalize) { auto finished = finalize(); if (!finished) return std::unexpected(finished.error()); }
        journal[QStringLiteral("Committed")] = true;
        return writeJournal(files[0].target, journal);
    };
    auto result = commit();
    if (!result) {
        preserveRecovery = true;
        for (size_t i = installed; i > 0; --i) {
            QFile::remove(staged[i - 1]);
            if (!QFile::copy(backups[i - 1], staged[i - 1])) return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Откат не завершён. Копии восстановления сохранены."), result.error().message});
            auto rolledBack = platform::replaceFile(staged[i - 1], files[i - 1].target);
            if (!rolledBack || !exactHash(files[i - 1].target, files[i - 1].before, {}))
                return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Откат не завершён. Копии восстановления сохранены."), result.error().message});
        }
        auto receipts = restoreReceipts(oldReceipts); if (!receipts) return std::unexpected(receipts.error());
        if (!QFile::remove(journalPath(files[0].target))) return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Откат выполнен, но журнал восстановления недоступен.")});
        preserveRecovery = false; return std::unexpected(result.error());
    }
    committed = true;
    if (!QFile::remove(journalPath(files[0].target))) { preserveRecovery = true; return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Файлы заменены. Журнал будет завершён при следующем запуске.")}); }
    return retainOriginal ? backups[0] : QString{};
}
}
QString gameDatabasePath(const QString& game) { return QDir(full(game)).filePath(QStringLiteral("BlueArchive_Data/StreamingAssets/PUB/Resource/Preload/TableBundles/ExcelDB.db")); }
Result<std::optional<QString>> recoverInstallation(const QString& game) {
    const auto database = gameDatabasePath(game), path = journalPath(database);
    if (!QFileInfo::exists(path)) return std::optional<QString>{};
    auto operation = platform::FileLock::operation(database); if (!operation) return std::unexpected(operation.error());
    auto closed = platform::requireGameClosed(); if (!closed) return std::unexpected(closed.error());
    auto safe = platform::requireSafeFile(path); if (!safe) return std::unexpected(safe.error());
    QFile input(path); if (!input.open(QIODevice::ReadOnly) || input.size() > 256 * 1024) return std::unexpected(invalid(QStringLiteral("Журнал восстановления недоступен.")));
    auto plain = platform::unprotectUser(input.readAll(), full(database).toUtf8()); input.close(); if (!plain) return std::unexpected(plain.error());
    const auto journal = QJsonDocument::fromJson(plain->bytes()).object();
    const auto id = journal.value(QStringLiteral("Id")).toString(), original = journal.value(QStringLiteral("OriginalBackup")).toString();
    const auto records = journal.value(QStringLiteral("Files")).toArray(), receipts = journal.value(QStringLiteral("OldReceipts")).toArray();
    const bool retain = journal.value(QStringLiteral("OriginalRetained")).toBool(), committed = journal.value(QStringLiteral("Committed")).toBool(), finished = journal.value(QStringLiteral("Finished")).toBool();
    static const QRegularExpression uuidN(QStringLiteral("^[0-9a-f]{32}$"));
    if (journal.value(QStringLiteral("FormatVersion")).toInt() != 1 || !uuidN.match(id).hasMatch() || (records.size() != 1 && records.size() != 3) || receipts.size() > 2)
        return std::unexpected(invalid(QStringLiteral("Журнал восстановления повреждён.")));
    const auto paths = targets(game, records.size() == 3); std::vector<std::unique_ptr<platform::FileLock>> locks;
    for (qsizetype i = 0; i < records.size(); ++i) {
        const auto item = records[i].toObject(); const auto target = paths[static_cast<size_t>(i)], backup = item.value(QStringLiteral("Backup")).toString(), stage = item.value(QStringLiteral("Stage")).toString();
        const auto before = item.value(QStringLiteral("Before")).toString(), after = item.value(QStringLiteral("After")).toString();
        const auto backupName = QFileInfo(target).fileName() + QStringLiteral(".baretool-backup-") + id + QStringLiteral(".db");
        if (!samePath(item.value(QStringLiteral("Target")).toString(), target) || !samePath(stage, target + QStringLiteral(".baretool-install-") + id + QStringLiteral(".tmp")) || !isHash(before) || !isHash(after) ||
            (retain ? QFileInfo(backup).fileName() != backupName : !samePath(backup, target + QStringLiteral(".baretool-rollback-") + id + QStringLiteral(".tmp"))))
            return std::unexpected(invalid(QStringLiteral("Небезопасные пути журнала восстановления.")));
        if (retain && i == 0 && !samePath(original, backup)) return std::unexpected(invalid(QStringLiteral("Некорректная оригинальная копия в журнале.")));
        if (QFileInfo::exists(stage)) { safe = platform::requireSafeFile(stage); if (!safe) return std::unexpected(safe.error()); }
        if (!finished || QFileInfo::exists(backup)) {
            auto sourceLock = platform::FileLock::read(backup); if (!sourceLock) return std::unexpected(sourceLock.error());
            locks.push_back(std::move(*sourceLock));
            auto copied = exactHash(backup, before, {}); if (!copied) return std::unexpected(copied.error());
        }
        auto targetLock = platform::FileLock::read(target, true); if (!targetLock) return std::unexpected(targetLock.error()); locks.push_back(std::move(*targetLock));
        auto current = hashFile(target); if (!current || (committed ? *current != after : *current != before && *current != after)) return std::unexpected(invalid(QStringLiteral("Файл игры изменён после прерванной операции. Восстановление остановлено; копии сохранены.")));
        auto allowed = sidecars(target); if (!allowed) return std::unexpected(allowed.error());
    }
    for (const auto& value : receipts) {
        const auto receipt = value.toObject(), old = receipt.value(QStringLiteral("Object")).toObject(); const auto receiptPath = receipt.value(QStringLiteral("Path")).toString();
        if (original.isEmpty() || old.value(QStringLiteral("FormatVersion")).toInt() != 1 || !old.value(QStringLiteral("OriginalVerified")).toBool() ||
            (!samePath(receiptPath, original + QStringLiteral(".baretool-backup.json")) && !samePath(receiptPath, original + QStringLiteral(".rut-bundle.json")))) return std::unexpected(invalid(QStringLiteral("Некорректные описания копии в журнале.")));
        safe = platform::requireSafeFile(receiptPath); if (!safe) return std::unexpected(safe.error());
    }
    if (!committed && !finished) {
        for (const auto& value : records) {
            const auto item = value.toObject(); const auto target = item.value(QStringLiteral("Target")).toString(), backup = item.value(QStringLiteral("Backup")).toString(), stage = item.value(QStringLiteral("Stage")).toString();
            const auto before = item.value(QStringLiteral("Before")).toString(); auto current = hashFile(target);
            if (current && *current == before) continue;
            QFile::remove(stage);
            if (!QFile::copy(backup, stage)) return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Не удалось подготовить аварийное восстановление. Копии сохранены.")});
            auto flushed = platform::flushFile(stage); if (!flushed) return std::unexpected(flushed.error());
            auto replaced = platform::replaceFile(stage, target); if (!replaced) return std::unexpected(replaced.error());
            auto checked = exactHash(target, before, {}); if (!checked) return std::unexpected(checked.error());
        }
        auto restored = restoreReceipts(receipts); if (!restored) return std::unexpected(restored.error());
    }
    auto completedJournal = journal; completedJournal[QStringLiteral("Finished")] = true;
    auto completed = writeJournal(database, completedJournal); if (!completed) return std::unexpected(completed.error());
    locks.clear();
    for (const auto& value : records) {
        const auto item = value.toObject(); QFile::remove(item.value(QStringLiteral("Stage")).toString());
        if (!committed || !retain) QFile::remove(item.value(QStringLiteral("Backup")).toString());
    }
    if (!committed && retain) { QFile::remove(original + QStringLiteral(".baretool-backup.json")); QFile::remove(original + QStringLiteral(".rut-bundle.json")); }
    if (!QFile::remove(path)) return std::unexpected(Error{ErrorCode::Io, QStringLiteral("Восстановление выполнено, но журнал не удалось завершить.")});
    return committed ? std::optional<QString>{original} : std::optional<QString>{};
}
Result<std::vector<QString>> installationHashes(const QString& game, bool bundle, const Cancellation& cancel) {
    std::vector<QString> result;
    for (const auto& path : targets(game, bundle)) {
        auto hash = hashFile(path, cancel);
        if (!hash) return std::unexpected(hash.error());
        result.push_back(*hash);
    }
    return result;
}
Result<std::vector<FileReplacement>> originalReceipt(const QString& game, const QString& backup, const Cancellation& cancel) {
    if (backup.isEmpty()) return std::unexpected(invalid(QStringLiteral("Подтверждённая исходная копия не найдена.")));
    auto doc = readObject(backup + QStringLiteral(".baretool-backup.json"));
        if (!doc) return std::unexpected(doc.error());
    if (doc->value(QStringLiteral("FormatVersion")).toInt() != 1 || !doc->value(QStringLiteral("OriginalVerified")).toBool() || doc->value(QStringLiteral("Forced")).toBool() ||
        !samePath(doc->value(QStringLiteral("TargetPath")).toString(), gameDatabasePath(game)))
        return std::unexpected(invalid(QStringLiteral("Эта копия не подтверждена как исходная база выбранной игры.")));
    std::vector<FileReplacement> result;
    if (QFileInfo::exists(backup + QStringLiteral(".rut-bundle.json"))) {
        auto bundle = readObject(backup + QStringLiteral(".rut-bundle.json"));
        if (!bundle) return std::unexpected(bundle.error());
        const auto items = bundle->value(QStringLiteral("Items")).toArray();
        if (bundle->value(QStringLiteral("FormatVersion")).toInt() != 1 || !bundle->value(QStringLiteral("OriginalVerified")).toBool() || items.size() != 3)
            return std::unexpected(invalid(QStringLiteral("Некорректное описание исходного общего пакета.")));
        const auto paths = targets(game, true);
        for (qsizetype i = 0; i < 3; ++i) {
            const auto item = items[i].toObject();
            if (!samePath(item.value(QStringLiteral("Target")).toString(), paths[static_cast<size_t>(i)]) || (i == 0 && !samePath(item.value(QStringLiteral("Backup")).toString(), backup)))
                return std::unexpected(invalid(QStringLiteral("Копия относится к другой установке игры.")));
            result.push_back({paths[static_cast<size_t>(i)], item.value(QStringLiteral("Backup")).toString(), item.value(QStringLiteral("AfterSha256")).toString(), item.value(QStringLiteral("BeforeSha256")).toString(), {}});
        }
    } else {
        if (doc->value(QStringLiteral("BeforeSha256")) == doc->value(QStringLiteral("AfterSha256"))) return std::unexpected(invalid(QStringLiteral("Копия не относится к установленному переводу.")));
        result.push_back({gameDatabasePath(game), backup, doc->value(QStringLiteral("AfterSha256")).toString(), doc->value(QStringLiteral("BeforeSha256")).toString(), {}});
    }
    for (const auto& item : result) {
        if (!isHash(item.before) || !isHash(item.after)) return std::unexpected(invalid(QStringLiteral("Некорректные хеши исходной копии.")));
        auto safe = platform::requireSafeFile(item.source);
        if (!safe) return std::unexpected(safe.error());
        auto source = exactHash(item.source, item.after, cancel), current = exactHash(item.target, item.before, cancel);
        if (!source) return std::unexpected(source.error());
        if (!current) return std::unexpected(current.error());
    }
    return result;
}
Result<void> restoreOriginal(const QString& game, const QString& backup, const Cancellation& cancel) {
    auto receipt = originalReceipt(game, backup, cancel);
        if (!receipt) return std::unexpected(receipt.error());
    auto restored = replace(*receipt, {}, false, [] { return platform::requireGameClosed(); }, cancel);
    if (!restored) return std::unexpected(restored.error());
    return {};
}
Result<InstallationResult> installPackage(const InstallationRequest& request, const CommitGuard& guard, const Cancellation& cancel) {
    auto valid = validatePreparedPackage(request.package.folder, cancel);
        if (!valid) return std::unexpected(valid.error());
    if (valid->patchedHash != request.package.patchedHash || valid->originalHash != request.package.originalHash || valid->bundle != request.package.bundle || valid->bundleHashes != request.package.bundleHashes || valid->signature != request.package.signature)
        return std::unexpected(invalid(QStringLiteral("Пакет изменился после выбора.")));
    if (!request.package.source.isEmpty()) { auto hash = exactHash(request.package.source, request.package.sourceHash, cancel);
        if (!hash) return std::unexpected(hash.error()); }
    auto actual = installationHashes(request.game, valid->bundle, cancel);
        if (!actual) return std::unexpected(actual.error());
    const auto paths = targets(request.game, valid->bundle);
    std::vector<QString> originals{valid->originalHash}, patched{valid->patchedHash}, sources{QDir(valid->folder).filePath(QStringLiteral("ExcelDB.db"))};
    if (valid->bundle) {
        originals.push_back(valid->bundleHashes[3]); originals.push_back(valid->bundleHashes[4]);
        patched.push_back(valid->bundleHashes[1]); patched.push_back(valid->bundleHashes[2]);
        sources.push_back(QDir(valid->folder).filePath(QStringLiteral("Excel.zip"))); sources.push_back(QDir(valid->folder).filePath(QStringLiteral("resources.assets")));
        for (size_t i = 0; i < sources.size(); ++i) if (QFileInfo(sources[i]).size() != QFileInfo(paths[i]).size()) return std::unexpected(invalid(QStringLiteral("Размер файла общего пакета отличается от исходного.")));
    }
    if (*actual == patched) return InstallationResult{request.previousOriginal, true};
    const bool forced = !request.approvedBefore.empty();
    if ((!forced && *actual != originals) || (forced && (request.approvedBefore != *actual || request.approvedAfter != valid->patchedHash)))
        return std::unexpected(invalid(forced ? QStringLiteral("Файлы изменились после подтверждения принудительной установки.") : QStringLiteral("Файлы игры отличаются от версии пакета. Требуется подтверждение принудительной установки.")));
    std::vector<FileReplacement> items;
    for (size_t i = 0; i < paths.size(); ++i) items.push_back({paths[i], sources[i], (*actual)[i], patched[i], {}});
    auto previous = originalReceipt(request.game, request.previousOriginal, cancel);
    bool keepPrevious = forced && previous && previous->size() == paths.size();
    if (keepPrevious) for (size_t i = 0; i < paths.size(); ++i) if ((*previous)[i].after != originals[i]) keepPrevious = false;
    CommitGuard finalize;
    QJsonArray oldReceipts;
    if (keepPrevious) {
        std::vector<FileReplacement> updated;
        for (size_t i = 0; i < paths.size(); ++i) updated.push_back({paths[i], {}, originals[i], patched[i], (*previous)[i].source});
        const auto databasePath = request.previousOriginal + QStringLiteral(".baretool-backup.json"), bundlePath = request.previousOriginal + QStringLiteral(".rut-bundle.json");
        auto oldDatabase = readObject(databasePath); if (!oldDatabase) return std::unexpected(oldDatabase.error());
        oldReceipts.append(QJsonObject{{QStringLiteral("Path"), databasePath}, {QStringLiteral("Object"), *oldDatabase}});
        if (paths.size() == 3) { auto oldBundle = readObject(bundlePath); if (!oldBundle) return std::unexpected(oldBundle.error()); oldReceipts.append(QJsonObject{{QStringLiteral("Path"), bundlePath}, {QStringLiteral("Object"), *oldBundle}}); }
        finalize = [updated, databasePath, bundlePath, old = *oldDatabase]() -> Result<void> {
            auto saved = writeObject(databasePath, databaseReceipt(updated[0], true)); if (!saved) return std::unexpected(saved.error());
            if (updated.size() == 3) {
                saved = writeObject(bundlePath, bundleReceipt(updated));
                if (!saved) { auto reverted = writeObject(databasePath, old); if (!reverted) return std::unexpected(reverted.error()); return std::unexpected(saved.error()); }
            }
            return {};
        };
    }
    auto backup = replace(items, request.backupRoot, !forced && request.originalVerified, guard, cancel, finalize, oldReceipts, keepPrevious ? request.previousOriginal : QString{});
    if (!backup) return std::unexpected(backup.error());
    if (keepPrevious) *backup = request.previousOriginal;
    return InstallationResult{*backup, false};
}
}
