#include <QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QScopeGuard>
#include <rutool/platform/Runtime.hpp>
#include "ClientController.hpp"
#include "UiState.hpp"

class UiTests final : public QObject {
    Q_OBJECT
    QTemporaryDir data_;
    std::unique_ptr<ClientController> client_;
    std::unique_ptr<UiState> state_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    QQuickWindow* window_{};
    QStringList warnings_;
    static QQuickItem* visualItem(QQuickItem* parent, const QString& name) {
        if (parent->objectName() == name) return parent;
        for (auto* child : parent->childItems())
            if (auto* found = visualItem(child, name)) return found;
        return nullptr;
    }
    QObject* item(const char* name) const {
        // Repeater delegates belong to the visual tree and need not be QObject children of the window.
        if (auto* found = window_->findChild<QObject*>(QString::fromLatin1(name))) return found;
        return visualItem(window_->contentItem(), QString::fromLatin1(name));
    }
    bool click(const char* name) const {
        auto* button = item(name);
        return button && button->property("enabled").toBool() && QMetaObject::invokeMethod(button, "clicked");
    }
    bool render(const QString& scene) {
        QTest::qWait(450);
        return window_->grabWindow().save(QDir::current().filePath(QStringLiteral("qml-211-%1.png").arg(scene)));
    }
private slots:
    void initTestCase() {
        QVERIFY(data_.isValid());
        client_ = std::make_unique<ClientController>();
        state_ = std::make_unique<UiState>(data_.filePath(QStringLiteral("settings.ini")), true);
        engine_ = std::make_unique<QQmlApplicationEngine>();
        connect(engine_.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError>& errors) {
            for (const auto& error : errors) warnings_.append(error.toString());
        });
        engine_->rootContext()->setContextProperty(QStringLiteral("client"), client_.get());
        engine_->rootContext()->setContextProperty(QStringLiteral("uiState"), state_.get());
        engine_->loadFromModule(QStringLiteral("RuTool.App"), QStringLiteral("Main"));
        QVERIFY(!engine_->rootObjects().isEmpty());
        window_ = qobject_cast<QQuickWindow*>(engine_->rootObjects().first()); QVERIFY(window_);
        QVERIFY(window_->isVisible()); QCOMPARE(client_->applicationVersion(), QStringLiteral(RUTOOL_APP_DISPLAY_VERSION));
    }
    void mandatoryAgreement() {
        auto* disclaimer = item("disclaimerWindow"); QVERIFY(disclaimer);
        QVERIFY(disclaimer->property("mandatory").toBool());
        QVERIFY(!item("disclaimerContinue")->property("enabled").toBool());
        QVERIFY(render(QStringLiteral("consent")));
        QVERIFY(item("agreementConsent")->setProperty("checked", true));
        QVERIFY(click("disclaimerContinue"));
        QVERIFY(state_->hasAcceptedAgreement(client_->licenseHash()));
        QVERIFY(QTest::qWaitFor([&] { return !disclaimer->property("visible").toBool(); }, 2000));
    }
    void linuxUpdateAndSettings() {
        QVERIFY(item("linuxUpdateButton"));
        QVERIFY(!item("applicationUpdateButton")->property("visible").toBool());
        client_->requestApplicationUpdate(); QVERIFY(!client_->busy());
        QCOMPARE(client_->operationStatus(), QStringLiteral("Проверка обновлений отключена в режиме проверки интерфейса."));
        QVERIFY(click("settingsButton"));
        auto* settings = item("settingsWindow"); QVERIFY(settings);
        QVERIFY(QTest::qWaitFor([&] { return settings->property("visible").toBool(); }, 2000));
        QVERIFY(item("gamePath")); QVERIFY(item("choosePackage")); QVERIFY(item("restoreButton"));
        QVERIFY(!item("restoreButton")->property("enabled").toBool());
        QVERIFY(click("darkThemeButton")); QVERIFY(state_->darkTheme());
        QVERIFY(render(QStringLiteral("settings-dark")));
        QVERIFY(click("lightThemeButton")); QVERIFY(!state_->darkTheme());
        QVERIFY(render(QStringLiteral("settings-light")));
        QVERIFY(QMetaObject::invokeMethod(settings, "close"));
        QVERIFY(QTest::qWaitFor([&] { return !settings->property("visible").toBool(); }, 2000));
    }
    void linuxReadingTagline() {
        auto* tagline = item("linuxReadingTagline"); QVERIFY(tagline);
        QVERIFY(tagline->property("visible").toBool());
        QCOMPARE(tagline->property("text").toString(), QStringLiteral("Sensei, I Can't Read"));
        const auto game = data_.filePath(QStringLiteral("reading-game"));
        const auto database = rutool::gameDatabasePath(game);
        const QByteArray original("original fixture"), translated("Russian! fixture");
        const auto hash = [](const QByteArray& bytes) {
            return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        };
        const auto write = [](const QString& path, const QByteArray& bytes) {
            if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        const auto oldRoot = client_->dataRoot_;
        const auto reset = qScopeGuard([&] {
            client_->operationalStarted_ = false;
            client_->operationalSettings_ = {};
            client_->backupPath_.clear();
            client_->dataRoot_ = oldRoot;
            client_->setGameFolder({});
        });
        const auto check = [&](bool installed) {
            QSignalSpy changed(client_.get(), &ClientController::changed);
            client_->refreshRestore();
            return QTest::qWaitFor([&] { return changed.count() > 0 && !client_->restoreWatcher_.isRunning(); }, 5000)
                && client_->translationInstalled() == installed;
        };
        client_->dataRoot_ = data_.path();
        QVERIFY(write(database, original));
        client_->setGameFolder(game);
        const auto key = rutool::platform::pathKey(game);
        client_->operationalSettings_[QStringLiteral("InstalledTranslationHashes")] =
            QJsonObject{{key, QJsonArray{hash(translated)}}};
        QVERIFY(check(false));
        QVERIFY(write(database, translated));
        QVERIFY(check(true)); // Also works without an original backup.
        QCOMPARE(tagline->property("text").toString(), QStringLiteral("Sensei, I Can Read!"));
        QVERIFY(render(QStringLiteral("reading")));
        client_->operationalStarted_ = true;
        client_->saveOperations();
        client_->operationalStarted_ = false;
        QFile settings(data_.filePath(QStringLiteral("client-v2-settings.json")));
        QVERIFY(settings.open(QIODevice::ReadOnly));
        client_->operationalSettings_ = QJsonDocument::fromJson(settings.readAll()).object();
        QVERIFY(check(true));
        client_->setGameFolder(data_.filePath(QStringLiteral("another-game")));
        QVERIFY(check(false));
        client_->setGameFolder(game);
        QVERIFY(check(true));
        QVERIFY(write(database, "updated by Steam"));
        QVERIFY(check(false));
        QCOMPARE(tagline->property("text").toString(), QStringLiteral("Sensei, I Can't Read"));

        // Old installations can be recognised through a verified backup receipt.
        client_->operationalSettings_ = {};
        const auto backup = data_.filePath(QStringLiteral("Backups/original.db"));
        QVERIFY(write(backup, original));
        const QJsonObject receipt{{QStringLiteral("FormatVersion"), 1}, {QStringLiteral("OriginalVerified"), true},
            {QStringLiteral("Forced"), false}, {QStringLiteral("TargetPath"), database},
            {QStringLiteral("BeforeSha256"), hash(original)}, {QStringLiteral("AfterSha256"), hash(translated)}};
        QVERIFY(write(backup + QStringLiteral(".baretool-backup.json"), QJsonDocument(receipt).toJson()));
        QVERIFY(write(database, translated));
        QVERIFY(check(true));
        QVERIFY(client_->canRestore());
        QVERIFY(rutool::restoreOriginal(game, backup));
        QVERIFY(check(false));
        QCOMPARE(tagline->property("text").toString(), QStringLiteral("Sensei, I Can't Read"));
    }
    void catalogAndAddon() {
        auto* catalog = item("translationsWindow"); QVERIFY(catalog);
        QVERIFY(catalog->setProperty("loadingScreensAvailable", true));
        QVERIFY(QMetaObject::invokeMethod(catalog, "show"));
        QVERIFY(QTest::qWaitFor([&] { return catalog->property("visible").toBool(); }, 2000));
        auto* action = item("loadingScreensAction"); QVERIFY(action);
        QVERIFY(action->property("visible").toBool());
        QCOMPARE(action->property("text").toString(), QStringLiteral("Ввести код бусти"));
        QSignalSpy code(catalog, SIGNAL(codeRequested()));
        QVERIFY(render(QStringLiteral("catalog")));
        QVERIFY(catalog->setProperty("primaryRelease", QVariantMap{{QStringLiteral("id"), QStringLiteral("public-fixture")},
            {QStringLiteral("name"), QStringLiteral("Основной тестовый перевод")}, {QStringLiteral("author"), QStringLiteral("Fixture")},
            {QStringLiteral("publishedAt"), QStringLiteral("24.11.2026 12:00")}}));
        QVERIFY(catalog->setProperty("releases", QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("premium-fixture")},
            {QStringLiteral("name"), QStringLiteral("Тестовый Premium")}, {QStringLiteral("author"), QStringLiteral("Fixture")},
            {QStringLiteral("publishedAt"), QStringLiteral("24.11.2026 11:00")}, {QStringLiteral("premium"), true}}}));
        QVERIFY(QTest::qWaitFor([&] { return item("translationDownload-public-fixture") && item("translationDownload-premium-fixture"); }, 2000));
        QVERIFY(item("premiumBadge-premium-fixture")->property("visible").toBool());
        QCOMPARE(item("translationDownload-public-fixture")->property("text").toString(), QStringLiteral("Скачать перевод"));
        QCOMPARE(item("translationDownload-premium-fixture")->property("text").toString(), QStringLiteral("Ввести код бусти"));
        state_->setDarkTheme(true); QVERIFY(render(QStringLiteral("catalog-dark"))); state_->setDarkTheme(false);
        QVERIFY(click("loadingScreensAction")); QCOMPARE(code.count(), 1);
        auto* boosty = item("boostyWindow"); QVERIFY(boosty);
        QVERIFY(QTest::qWaitFor([&] { return boosty->property("visible").toBool(); }, 2000));
        QVERIFY(QMetaObject::invokeMethod(boosty, "close"));
        QVERIFY(QTest::qWaitFor([&] { return !boosty->property("visible").toBool(); }, 2000));
        client_->downloadAndInstallLoadingScreens(); // Invalid game and no Premium grant: no work may start.
        QVERIFY(!client_->busy());
    }
    void reportAndResize() {
        auto* report = item("reportWindow"); QVERIFY(report);
        QVERIFY(QMetaObject::invokeMethod(report, "show"));
        QVERIFY(QTest::qWaitFor([&] { return report->property("visible").toBool(); }, 2000));
        for (const auto& size : {QSize(940, 700), QSize(1280, 900)}) {
            window_->resize(size);
            QVERIFY(render(QStringLiteral("report-%1").arg(size.width())));
            auto* panel = qobject_cast<QQuickItem*>(report); QVERIFY(panel);
            QCOMPARE(panel->height(), static_cast<qreal>(window_->height()));
            QVERIFY(panel->width() <= window_->width());
        }
        QVERIFY(QMetaObject::invokeMethod(report, "close"));
        QVERIFY(QTest::qWaitFor([&] { return !report->property("visible").toBool(); }, 2000));
        QVERIFY(render(QStringLiteral("launcher")));
    }
    void cleanupTestCase() {
        engine_.reset(); state_.reset(); client_.reset();
        QVERIFY2(warnings_.isEmpty(), qPrintable(warnings_.join('\n')));
    }
};
int main(int argc, char** argv) {
    QTemporaryDir isolated;
    if (!isolated.isValid()) return 2;
    for (const auto* name : {"XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME"}) qputenv(name, isolated.path().toUtf8());
    QGuiApplication app(argc, argv);
    app.setProperty("rutoolSmokeTest", true);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    UiTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "UiTests.moc"
