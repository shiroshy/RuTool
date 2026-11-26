#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcess>
#include <QScopeGuard>
#include <QCryptographicHash>
#include <rutool/platform/Runtime.hpp>
#include <rutool/platform/FileOps.hpp>
#include <rutool/platform/Crypto.hpp>
#include <rutool/core/Installation.hpp>
#include <rutool/core/InstallerConfig.hpp>
#include <rutool/platform/AppImageUpdates.hpp>
#include "TestFiles.hpp"
#include "TestNetwork.hpp"
#include "TestImages.hpp"
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <cerrno>
#include <QTimer>
#include <cstring>
#include <unistd.h>

// Linux faults and process fixtures stay out of the shared workflow tests.
// Neither the application nor CoreTests links these syscall overrides.
namespace {
thread_local const char* syncFailureTarget{};
thread_local const char* syncFailurePrefix{"translat"};
thread_local off_t syncFailureOffset{};
thread_local bool corruptAfterRename{};
thread_local bool crashAfterRename{};
thread_local const char* syncFailureFile{};
thread_local const char* renameFailureName{};
thread_local bool renameBeforeFailure{};
thread_local const char* changeBeforeCommit{};
thread_local const char* failCommitJournalTarget{};
}
extern "C" int fsync(int fd) {
    if (failCommitJournalTarget) {
        const auto journal = QByteArray(failCommitJournalTarget) + ".baretool-transaction.dpapi";
        struct stat actual{}, expected{};
        if (::fstat(fd, &actual) == 0 && ::stat(journal.constData(), &expected) == 0 &&
            actual.st_dev == expected.st_dev && actual.st_ino == expected.st_ino) {
            const int target = ::open(failCommitJournalTarget, O_RDONLY | O_CLOEXEC);
            char prefix[8]{};
            const auto count = target >= 0 ? ::read(target, prefix, sizeof(prefix)) : -1;
            if (target >= 0) ::close(target);
            if (count == 8 && std::memcmp(prefix, "translat", 8) == 0) {
                failCommitJournalTarget = nullptr;
                // Also interrupt rollback after it restores the original bytes.
                renameFailureName = "ExcelDB.db"; renameBeforeFailure = true;
                errno = EIO; return -1;
            }
        }
    }
    if (changeBeforeCommit) {
        const auto journal = QByteArray(changeBeforeCommit) + ".baretool-transaction.dpapi";
        if (::access(journal.constData(), F_OK) == 0) {
            const int changed = ::open(changeBeforeCommit, O_WRONLY | O_TRUNC | O_CLOEXEC);
            changeBeforeCommit = nullptr;
            if (changed >= 0) {
                const auto written = ::write(changed, "unknown contents", 16);
                ::close(changed);
                if (written != 16) return -1;
            }
        }
    }
    if (syncFailureFile) {
        struct stat actual{}, expected{};
        if (::fstat(fd, &actual) == 0 && ::stat(syncFailureFile, &expected) == 0 &&
            actual.st_dev == expected.st_dev && actual.st_ino == expected.st_ino) {
            syncFailureFile = nullptr; errno = EIO; return -1;
        }
    }
    if (syncFailureTarget) {
        struct stat info{};
        if (::fstat(fd, &info) == 0 && S_ISDIR(info.st_mode)) {
            const int targetFd = ::open(syncFailureTarget, O_RDONLY | O_CLOEXEC);
            if (targetFd >= 0 && syncFailureOffset) ::lseek(targetFd, syncFailureOffset, SEEK_SET);
            char prefix[8]{};
            const auto count = targetFd >= 0 ? ::read(targetFd, prefix, sizeof(prefix)) : -1;
            if (targetFd >= 0) ::close(targetFd);
            if (count == 8 && std::memcmp(prefix, syncFailurePrefix, 8) == 0) {
                if (crashAfterRename) ::_exit(77);
                if (corruptAfterRename) {
                    const int changed = ::open(syncFailureTarget, O_WRONLY | O_TRUNC | O_CLOEXEC);
                    if (changed >= 0) {
                        const auto written = ::write(changed, "unknown contents", 16);
                        ::close(changed);
                        if (written != 16) return -1;
                    }
                }
                syncFailureTarget = nullptr; errno = EIO; return -1;
            }
        }
    }
    return static_cast<int>(::syscall(SYS_fsync, fd));
}
extern "C" int renameat(int from, const char* source, int to, const char* target) {
    if (renameFailureName && std::strcmp(target, renameFailureName) == 0) {
        renameFailureName = nullptr;
        if (renameBeforeFailure && ::syscall(SYS_renameat, from, source, to, target) != 0) return -1;
        errno = EIO; return -1;
    }
    return static_cast<int>(::syscall(SYS_renameat, from, source, to, target));
}
using namespace rutool;
using namespace rutool::test;
class LinuxTests : public QObject {
    Q_OBJECT
private slots:
    void appImageReplacementFailures() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto current = temp.filePath(QStringLiteral("RuTool.AppImage"));
        const auto original = image("original version"), replacement = image("translated version");
        FixtureTransport transport; transport.payload = replacement;
        LinuxRelease release{QStringLiteral("2.2.0"),
            QString::fromLatin1(QCryptographicHash::hash(replacement, QCryptographicHash::Sha256).toHex()),
            replacement.size(), QUrl(QStringLiteral("https://github.com/example/RuTool/releases/download/v2.2.0/RuTool.AppImage"))};
        const auto encoded = QFile::encodeName(current);
        const auto reset = qScopeGuard([] {
            renameFailureName = nullptr; renameBeforeFailure = false;
            syncFailureTarget = nullptr; syncFailureOffset = 0;
        });
        for (int failure = 0; failure < 3; ++failure) {
            QVERIFY(write(current, original));
            QVERIFY(QFile::setPermissions(current, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
            if (failure < 2) { renameFailureName = "RuTool.AppImage"; renameBeforeFailure = failure == 1; }
            else { syncFailureTarget = encoded.constData(); syncFailureOffset = 64; }
            auto result = platform::installAppImageUpdate(release, current, current, *hashFile(current), {}, {}, &transport);
            QVERIFY(!result);
            if (failure < 2) QVERIFY(renameFailureName == nullptr);
            else QVERIFY(syncFailureTarget == nullptr);
            QCOMPARE(read(current), failure == 0 ? original : replacement);
            const auto previous = current + QStringLiteral(".rutool-previous.AppImage");
            QCOMPARE(read(previous), original);
            QVERIFY(QFile::permissions(previous).testFlag(QFileDevice::ExeOwner));
        }
    }
    void installerConfiguration() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto previous = qgetenv("XDG_CONFIG_HOME");
        const auto restore = qScopeGuard([&] {
            if (previous.isNull()) qunsetenv("XDG_CONFIG_HOME");
            else qputenv("XDG_CONFIG_HOME", previous);
        });
        qputenv("XDG_CONFIG_HOME", temp.path().toUtf8());
        const auto name = QStringLiteral("installer-keys.json");
        const auto bundled = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("app/config/") + name);
        QCOMPARE(installerConfigPath(name), bundled);
        QVERIFY(readPackagePassword(bundled));
    }

    void localProtection() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); qputenv("XDG_DATA_HOME", temp.path().toUtf8());
        auto cipher = platform::protectUser("secret fixture", "context"); QVERIFY(cipher);
        auto again = platform::protectUser("secret fixture", "context"); QVERIFY(again); QVERIFY(*cipher != *again);
        auto plain = platform::unprotectUser(*cipher, "context"); QVERIFY(plain); QCOMPARE(plain->bytes(), QByteArray("secret fixture"));
        QVERIFY(!platform::unprotectUser(*cipher, "other context"));
        auto tampered = *cipher; tampered[tampered.size() - 1] ^= 1;
        QVERIFY(!platform::unprotectUser(tampered, "context"));
        const auto path = QDir(temp.path()).filePath(QStringLiteral("RuToolInstaller/private/local.key"));
        struct stat st{}; QVERIFY(::stat(QFile::encodeName(path).constData(), &st) == 0); QCOMPARE(st.st_mode & 0777, mode_t{0600});
        QVERIFY(::chmod(QFile::encodeName(path).constData(), 0644) == 0);
        QVERIFY(!platform::unprotectUser(*cipher, "context"));
        QVERIFY(::chmod(QFile::encodeName(path).constData(), 0600) == 0);
        const auto originalKey = read(path);
        const auto alias = QFile::encodeName(path + QStringLiteral(".alias"));
        QVERIFY(::link(QFile::encodeName(path).constData(), alias.constData()) == 0);
        QVERIFY(!platform::protectUser("new data", "context"));
        QCOMPARE(read(path), originalKey);
        QVERIFY(QFile::remove(QString::fromLocal8Bit(alias)));
        QVERIFY(write(path, "damaged key"));
        QVERIFY(!platform::protectUser("new data", "context"));
        QCOMPARE(read(path), QByteArray("damaged key"));
        QVERIFY(write(path, originalKey));
        QVERIFY(QFile::remove(path)); QVERIFY(!platform::unprotectUser(*cipher, "context"));
        const auto id = platform::installationDeviceId(); QVERIFY(id); QCOMPARE(id->size(), qsizetype{64});
    }
    void pathsLocksAndReplacement() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); const QDir dir(temp.path());
        const auto target = dir.filePath(QStringLiteral("target")), staged = dir.filePath(QStringLiteral("stage"));
        QVERIFY(write(target, "old")); QVERIFY(write(staged, "new"));
        QVERIFY(platform::requireSafeFile(target));
        QVERIFY(!platform::requireSafeFile(target + QChar::Null + QStringLiteral("hidden")));
        QVERIFY(::chmod(QFile::encodeName(target).constData(), 0640) == 0);
        QVERIFY(QFile::link(target, dir.filePath(QStringLiteral("link")))); QVERIFY(!platform::requireSafeFile(dir.filePath(QStringLiteral("link"))));
        QVERIFY(QFile::link(temp.path(), dir.filePath(QStringLiteral("dirlink"))));
        QVERIFY(!platform::requireSafeFile(dir.filePath(QStringLiteral("dirlink/target"))));
        const auto hard = QFile::encodeName(dir.filePath(QStringLiteral("hard")));
        QVERIFY(::link(QFile::encodeName(target).constData(), hard.constData()) == 0); QVERIFY(!platform::requireSafeFile(target)); QVERIFY(QFile::remove(QString::fromLocal8Bit(hard)));
        auto first = platform::FileLock::operation(target); QVERIFY(first); QVERIFY(!platform::FileLock::operation(target));
        first->reset(); QVERIFY(platform::FileLock::operation(target));
        QProcess peer;
        peer.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--lock-process-fixture"), target});
        QVERIFY(peer.waitForStarted()); QVERIFY(peer.waitForReadyRead());
        const bool peerBlocked = !platform::FileLock::operation(target);
        peer.kill(); QVERIFY(peer.waitForFinished()); QVERIFY(peerBlocked);
        QVERIFY(platform::FileLock::operation(target));
        auto cache = platform::FileLock::cacheEntry(dir.filePath(QStringLiteral("cache.rut"))); QVERIFY(cache); QVERIFY(!platform::FileLock::cacheEntry(dir.filePath(QStringLiteral("cache.rut"))));
        auto lease = platform::FileLock::directoryLease(temp.path()); QVERIFY(lease); QVERIFY(!platform::FileLock::directoryLease(temp.path()));
        QVERIFY(platform::flushFile(staged)); QVERIFY(platform::replaceFile(staged, target)); QCOMPARE(read(target), QByteArray("new")); QVERIFY(!QFileInfo::exists(staged));
        struct stat targetMode{}; QVERIFY(::stat(QFile::encodeName(target).constData(), &targetMode) == 0);
        QCOMPARE(targetMode.st_mode & 0777, mode_t{0640});
        QVERIFY(!platform::replaceFile(dir.filePath(QStringLiteral("link")), target));
        QVERIFY(platform::requireFreeSpace({{target, 1}})); QVERIFY(!platform::requireFreeSpace({{target, std::numeric_limits<quint64>::max()}}));
        QVERIFY(platform::currentProcessStartedTicks());
    }
    void replacementFailureStates() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto target = temp.filePath(QStringLiteral("target"));
        const auto stage = temp.filePath(QStringLiteral("stage"));
        const auto encodedStage = QFile::encodeName(stage);
        const auto encodedTarget = QFile::encodeName(target);
        const auto reset = qScopeGuard([] {
            syncFailureFile = nullptr; syncFailureTarget = nullptr;
            renameFailureName = nullptr; renameBeforeFailure = false;
        });
        QVERIFY(write(target, "original"));
        auto missing = platform::replaceFile(stage, target);
        QVERIFY(!missing); QCOMPARE(missing.error().state, platform::ReplacementState::NotReplaced);
        QCOMPARE(read(target), QByteArray("original"));

        QVERIFY(write(stage, "translated"));
        syncFailureFile = encodedStage.constData();
        auto beforeRename = platform::replaceFile(stage, target);
        QVERIFY(!beforeRename); QVERIFY(syncFailureFile == nullptr);
        QCOMPARE(beforeRename.error().state, platform::ReplacementState::NotReplaced);
        QCOMPARE(read(target), QByteArray("original")); QCOMPARE(read(stage), QByteArray("translated"));

        syncFailureTarget = encodedTarget.constData();
        auto afterRename = platform::replaceFile(stage, target);
        QVERIFY(!afterRename); QVERIFY(syncFailureTarget == nullptr);
        QCOMPARE(afterRename.error().state, platform::ReplacementState::Replaced);
        QCOMPARE(read(target), QByteArray("translated")); QVERIFY(!QFileInfo::exists(stage));

        for (const bool didRename : {false, true}) {
            QVERIFY(write(target, "original")); QVERIFY(write(stage, "translated"));
            renameFailureName = "target"; renameBeforeFailure = didRename;
            auto uncertain = platform::replaceFile(stage, target);
            QVERIFY(!uncertain); QVERIFY(renameFailureName == nullptr);
            QCOMPARE(uncertain.error().state, platform::ReplacementState::Unknown);
            QCOMPARE(read(target), QByteArray(didRename ? "translated" : "original"));
            QCOMPARE(QFileInfo::exists(stage), !didRename);
        }
    }
    void uncertainReplacementRollback() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); qputenv("XDG_DATA_HOME", temp.path().toUtf8());
        const auto folder = temp.filePath(QStringLiteral("prepared")); QVERIFY(QDir().mkpath(folder));
        platform::Secret password(QByteArray("public-test-password"));
        auto package = decryptPackage(fixture("sample.rut.fixture"), folder, password); QVERIFY(package);
        InstallationRequest request; request.game = temp.filePath(QStringLiteral("game"));
        request.package = *package; request.backupRoot = temp.filePath(QStringLiteral("backups"));
        request.originalVerified = true;
        const auto target = gameDatabasePath(request.game);
        const auto reset = qScopeGuard([] { renameFailureName = nullptr; renameBeforeFailure = false; });
        for (const bool didRename : {false, true}) {
            QVERIFY(write(target, read(fixture("original.db.fixture"))));
            renameFailureName = "ExcelDB.db"; renameBeforeFailure = didRename;
            auto result = installPackage(request, []() -> Result<void> { return {}; });
            QVERIFY(!result); QVERIFY(renameFailureName == nullptr);
            QCOMPARE(read(target), read(fixture("original.db.fixture")));
            QVERIFY(!QFileInfo::exists(target + QStringLiteral(".baretool-transaction.dpapi")));
        }
    }
    void changedTargetBeforeCommit() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); qputenv("XDG_DATA_HOME", temp.path().toUtf8());
        const auto folder = temp.filePath(QStringLiteral("prepared")); QVERIFY(QDir().mkpath(folder));
        platform::Secret password(QByteArray("public-test-password"));
        auto package = decryptPackage(fixture("sample.rut.fixture"), folder, password); QVERIFY(package);
        InstallationRequest request; request.game = temp.filePath(QStringLiteral("game"));
        request.package = *package; request.backupRoot = temp.filePath(QStringLiteral("backups"));
        request.originalVerified = true;
        const auto target = gameDatabasePath(request.game);
        QVERIFY(write(target, read(fixture("original.db.fixture"))));
        const auto encodedTarget = QFile::encodeName(target);
        const auto reset = qScopeGuard([] { changeBeforeCommit = nullptr; });
        changeBeforeCommit = encodedTarget.constData();
        auto result = installPackage(request, []() -> Result<void> { return {}; });
        QVERIFY(!result); QVERIFY(changeBeforeCommit == nullptr);
        QCOMPARE(read(target), QByteArray("unknown contents"));
        const auto journal = target + QStringLiteral(".baretool-transaction.dpapi");
        QVERIFY(QFileInfo::exists(journal)); QVERIFY(!recoverInstallation(request.game));
        bool originalPreserved{};
        for (const auto& backup : QDir(request.backupRoot).entryList({QStringLiteral("*.db")}, QDir::Files))
            originalPreserved = originalPreserved || read(QDir(request.backupRoot).filePath(backup)) == read(fixture("original.db.fixture"));
        QVERIFY(originalPreserved);
        QVERIFY(write(target, read(fixture("original.db.fixture"))));
        QVERIFY(recoverInstallation(request.game)); QVERIFY(!QFileInfo::exists(journal));
    }
    void commitJournalFailureThenInterruptedRollback() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); qputenv("XDG_DATA_HOME", temp.path().toUtf8());
        const auto folder = temp.filePath(QStringLiteral("prepared")); QVERIFY(QDir().mkpath(folder));
        platform::Secret password(QByteArray("public-test-password"));
        auto package = decryptPackage(fixture("sample.rut.fixture"), folder, password); QVERIFY(package);
        InstallationRequest request; request.game = temp.filePath(QStringLiteral("game"));
        request.package = *package; request.backupRoot = temp.filePath(QStringLiteral("backups"));
        request.originalVerified = true;
        const auto target = gameDatabasePath(request.game);
        const auto original = read(fixture("original.db.fixture"));
        QVERIFY(write(target, original));
        const auto encodedTarget = QFile::encodeName(target);
        const auto reset = qScopeGuard([] {
            failCommitJournalTarget = nullptr; renameFailureName = nullptr; renameBeforeFailure = false;
        });
        failCommitJournalTarget = encodedTarget.constData();
        auto installed = installPackage(request, []() -> Result<void> { return {}; });
        QVERIFY(!installed); QVERIFY(failCommitJournalTarget == nullptr); QVERIFY(renameFailureName == nullptr);
        QCOMPARE(read(target), original);
        const auto journal = target + QStringLiteral(".baretool-transaction.dpapi");
        QVERIFY(QFileInfo::exists(journal));
        auto plain = platform::unprotectUser(read(journal), target.toUtf8()); QVERIFY(plain);
        const auto object = QJsonDocument::fromJson(plain->bytes()).object();
        QVERIFY(object.contains(QStringLiteral("Committed")));
        QVERIFY(!object.value(QStringLiteral("Committed")).toBool());
        QVERIFY(recoverInstallation(request.game));
        QCOMPARE(read(target), original);
        QVERIFY(!QFileInfo::exists(journal));
    }
    void steamDiscovery() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); const QDir dir(temp.path());
        const auto steam = dir.filePath(QStringLiteral("Steam")), library = dir.filePath(QStringLiteral("Library With Spaces"));
        const auto game = QDir(library).filePath(QStringLiteral("steamapps/common/Blue Archive"));
        QVERIFY(write(gameDatabasePath(game), "fixture"));
        QVERIFY(write(QDir(steam).filePath(QStringLiteral("steamapps/libraryfolders.vdf")), QByteArray("\"libraryfolders\" { \"0\" { \"path\" \"") + library.toUtf8() + "\" } }"));
        QVERIFY(write(QDir(library).filePath(QStringLiteral("steamapps/appmanifest_123.acf")), "\"AppState\" { \"installdir\" \"Blue Archive\" }"));
        QCOMPARE(platform::discoverGameInstallations({steam}), QStringList{game});
        const auto proton = QDir(steam).filePath(QStringLiteral("steamapps/compatdata/456/pfx/drive_c/Games/BlueArchive"));
        QVERIFY(write(gameDatabasePath(proton), "fixture"));
        const auto found = platform::discoverGameInstallations({steam}); QCOMPARE(found.size(), qsizetype{2}); QVERIFY(found.contains(proton));
        QVERIFY(platform::pathKey(QStringLiteral("A")) != platform::pathKey(QStringLiteral("a")));
    }
    void processGuard() {
        QVERIFY(platform::requireGameClosed());
        QProcess game;
        game.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--game-process-fixture")});
        QVERIFY(game.waitForStarted()); QVERIFY(game.waitForReadyRead());
        const bool detected = !platform::requireGameClosed(); game.kill(); game.waitForFinished(); QVERIFY(detected);
    }
    void installationDurabilityFailures() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); qputenv("XDG_DATA_HOME", temp.path().toUtf8());
        const auto prepared = temp.filePath(QStringLiteral("prepared")); QVERIFY(QDir().mkpath(prepared));
        platform::Secret password(QByteArray("public-test-password"));
        auto package = decryptPackage(fixture("sample.rut.fixture"), prepared, password); QVERIFY(package);
        InstallationRequest request; request.game = temp.filePath(QStringLiteral("game")); request.package = *package;
        request.backupRoot = temp.filePath(QStringLiteral("backups")); request.originalVerified = true;
        const auto database = gameDatabasePath(request.game);
        QVERIFY(write(database, read(fixture("original.db.fixture"))));
        const auto guard = []() -> Result<void> { return {}; };
        const auto reset = qScopeGuard([] { syncFailureTarget = nullptr; corruptAfterRename = false; });
        const auto encodedTarget = QFile::encodeName(database);
        syncFailureTarget = encodedTarget.constData();
        auto durabilityFailure = installPackage(request, guard);
        const bool injected = syncFailureTarget == nullptr; syncFailureTarget = nullptr;
        QVERIFY(injected); QVERIFY(!durabilityFailure);
        QCOMPARE(read(database), read(fixture("original.db.fixture")));
        QVERIFY(!QFileInfo::exists(database + QStringLiteral(".baretool-transaction.dpapi")));
        // Unknown bytes after a failed replacement must keep recovery data.
        syncFailureTarget = encodedTarget.constData(); corruptAfterRename = true;
        auto unknownFailure = installPackage(request, guard);
        const bool unknownInjected = syncFailureTarget == nullptr;
        syncFailureTarget = nullptr; corruptAfterRename = false;
        QVERIFY(unknownInjected); QVERIFY(!unknownFailure);
        const auto journal = database + QStringLiteral(".baretool-transaction.dpapi");
        QVERIFY(QFileInfo::exists(journal));
        QVERIFY(!recoverInstallation(request.game));
        QVERIFY(QFileInfo::exists(journal));
        bool originalPreserved{};
        for (const auto& backup : QDir(request.backupRoot).entryList({QStringLiteral("*.db")}, QDir::Files))
            originalPreserved = originalPreserved || read(QDir(request.backupRoot).filePath(backup)) == read(fixture("original.db.fixture"));
        QVERIFY(originalPreserved);
        QVERIFY(write(database, read(fixture("patched.db.fixture"))));
        QVERIFY(recoverInstallation(request.game));
        QCOMPARE(read(database), read(fixture("original.db.fixture")));
        QVERIFY(!QFileInfo::exists(journal));
    }
    void abruptInstallerExit() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        qputenv("XDG_DATA_HOME", temp.path().toUtf8());
        const auto game = temp.filePath(QStringLiteral("game"));
        const auto database = gameDatabasePath(game);
        QVERIFY(write(database, read(fixture("original.db.fixture"))));
        QProcess installer;
        installer.start(QCoreApplication::applicationFilePath(),
            {QStringLiteral("--install-crash-fixture"), game, temp.filePath(QStringLiteral("backups"))});
        QVERIFY(installer.waitForFinished(15000));
        QCOMPARE(installer.exitCode(), 77);
        QCOMPARE(read(database), read(fixture("patched.db.fixture")));
        const auto journal = database + QStringLiteral(".baretool-transaction.dpapi");
        QVERIFY(QFileInfo::exists(journal));
        QVERIFY(recoverInstallation(game));
        QCOMPARE(read(database), read(fixture("original.db.fixture")));
        QVERIFY(!QFileInfo::exists(journal));
    }
    void recoveryJournal() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); qputenv("XDG_DATA_HOME", temp.path().toUtf8());
        const auto game = QDir(temp.path()).filePath(QStringLiteral("game")), target = gameDatabasePath(game);
        const QString id(32, 'a'); const auto backup = target + QStringLiteral(".baretool-rollback-") + id + QStringLiteral(".tmp");
        const auto stage = target + QStringLiteral(".baretool-install-") + id + QStringLiteral(".tmp");
        QVERIFY(write(target, read(fixture("patched.db.fixture")))); QVERIFY(write(backup, read(fixture("original.db.fixture"))));
        const auto before = hashFile(backup), after = hashFile(target); QVERIFY(before); QVERIFY(after);
        const QJsonObject item{{QStringLiteral("Target"), target}, {QStringLiteral("Backup"), backup}, {QStringLiteral("Stage"), stage},
            {QStringLiteral("Before"), *before}, {QStringLiteral("After"), *after}};
        const QJsonObject journal{{QStringLiteral("FormatVersion"), 1}, {QStringLiteral("Id"), id}, {QStringLiteral("Files"), QJsonArray{item}},
            {QStringLiteral("OriginalRetained"), false}, {QStringLiteral("Committed"), false}};
        auto encrypted = platform::protectUser(QJsonDocument(journal).toJson(QJsonDocument::Compact), target.toUtf8()); QVERIFY(encrypted);
        const auto path = target + QStringLiteral(".baretool-transaction.dpapi");
        QVERIFY(write(path, *encrypted)); auto corrupted = *encrypted; corrupted[34] ^= 1; QVERIFY(write(path, corrupted));
        QVERIFY(!recoverInstallation(game)); QCOMPARE(read(target), read(fixture("patched.db.fixture"))); QVERIFY(QFileInfo::exists(backup));
        QVERIFY(write(path, *encrypted));
        const auto key = temp.filePath(QStringLiteral("RuToolInstaller/private/local.key"));
        QVERIFY(QFile::rename(key, key + QStringLiteral(".saved")));
        QVERIFY(!recoverInstallation(game));
        QVERIFY(!QFileInfo::exists(key)); QCOMPARE(read(path), *encrypted);
        QCOMPARE(read(target), read(fixture("patched.db.fixture"))); QVERIFY(QFileInfo::exists(backup));
        QVERIFY(QFile::rename(key + QStringLiteral(".saved"), key));
        QVERIFY(recoverInstallation(game)); QCOMPARE(read(target), read(fixture("original.db.fixture")));
        QVERIFY(!QFileInfo::exists(path)); QVERIFY(!QFileInfo::exists(backup));
    }
    void recoveryRetriesDurability() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); qputenv("XDG_DATA_HOME", temp.path().toUtf8());
        const auto game = temp.filePath(QStringLiteral("game")), target = gameDatabasePath(game);
        const QString id(32, 'd');
        const auto backup = target + QStringLiteral(".baretool-rollback-") + id + QStringLiteral(".tmp");
        const auto stage = target + QStringLiteral(".baretool-install-") + id + QStringLiteral(".tmp");
        QVERIFY(write(target, read(fixture("patched.db.fixture"))));
        QVERIFY(write(backup, read(fixture("original.db.fixture"))));
        const auto before = hashFile(backup), after = hashFile(target); QVERIFY(before); QVERIFY(after);
        const QJsonObject item{{QStringLiteral("Target"), target}, {QStringLiteral("Backup"), backup}, {QStringLiteral("Stage"), stage},
            {QStringLiteral("Before"), *before}, {QStringLiteral("After"), *after}};
        const QJsonObject journal{{QStringLiteral("FormatVersion"), 1}, {QStringLiteral("Id"), id}, {QStringLiteral("Files"), QJsonArray{item}},
            {QStringLiteral("OriginalRetained"), false}, {QStringLiteral("Committed"), false}};
        auto encrypted = platform::protectUser(QJsonDocument(journal).toJson(QJsonDocument::Compact), target.toUtf8()); QVERIFY(encrypted);
        const auto path = target + QStringLiteral(".baretool-transaction.dpapi"); QVERIFY(write(path, *encrypted));
        const auto encodedTarget = QFile::encodeName(target);
        const auto reset = qScopeGuard([] {
            syncFailureTarget = nullptr; syncFailureFile = nullptr; syncFailurePrefix = "translat";
        });
        syncFailurePrefix = "original"; syncFailureTarget = encodedTarget.constData();
        QVERIFY(!recoverInstallation(game)); QVERIFY(syncFailureTarget == nullptr);
        QCOMPARE(read(target), read(fixture("original.db.fixture")));
        QVERIFY(QFileInfo::exists(path)); QVERIFY(QFileInfo::exists(backup));
        // Matching bytes alone do not make the previous rename durable.
        syncFailureTarget = encodedTarget.constData();
        QVERIFY(!recoverInstallation(game)); QVERIFY(syncFailureTarget == nullptr);
        QVERIFY(QFileInfo::exists(path)); QVERIFY(QFileInfo::exists(backup));
        QVERIFY(recoverInstallation(game));
        QCOMPARE(read(target), read(fixture("original.db.fixture")));
        QVERIFY(!QFileInfo::exists(path)); QVERIFY(!QFileInfo::exists(backup));
    }
};
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() == 3 && arguments[1] == QStringLiteral("--lock-process-fixture")) {
        auto lock = platform::FileLock::operation(arguments[2]);
        if (!lock || ::write(STDOUT_FILENO, "ready", 5) != 5) return 1;
        QTimer::singleShot(20000, &app, &QCoreApplication::quit); return app.exec();
    }
    if (arguments.size() == 4 && arguments[1] == QStringLiteral("--install-crash-fixture")) {
        QTemporaryDir prepared(QDir(arguments[2]).filePath(QStringLiteral("prepared-XXXXXX")));
        if (!prepared.isValid()) return 1;
        platform::Secret password(QByteArray("public-test-password"));
        auto package = decryptPackage(fixture("sample.rut.fixture"), prepared.path(), password);
        if (!package) return 1;
        InstallationRequest request; request.game = arguments[2]; request.package = *package;
        request.backupRoot = arguments[3]; request.originalVerified = true;
        const auto target = QFile::encodeName(gameDatabasePath(request.game));
        syncFailureTarget = target.constData(); crashAfterRename = true;
        auto result = installPackage(request, []() -> Result<void> { return {}; });
        return result ? 0 : 2; // The injected abrupt exit must terminate with 77.
    }
    if (app.arguments().contains(QStringLiteral("--game-process-fixture"))) {
        if (::prctl(PR_SET_NAME, "BlueArchive.exe", 0, 0, 0) != 0) return 1;
        if (::write(STDOUT_FILENO, "ready", 5) != 5) return 1;
        QTimer::singleShot(20000, &app, &QCoreApplication::quit); return app.exec();
    }
    LinuxTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "LinuxTests.moc"
