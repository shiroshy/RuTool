#include "ExternalLinks.hpp"
#include <QDesktopServices>
#include <QProcess>
#include <QStandardPaths>

QProcessEnvironment ExternalLinks::desktopEnvironment(QProcessEnvironment environment) {
    if (environment.value(QStringLiteral("RUTOOL_DESKTOP_ENV_SAVED")) != QStringLiteral("1")) return environment;
    for (const auto* name : {"LD_LIBRARY_PATH", "LD_PRELOAD", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH",
                             "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "QT_QUICK_CONTROLS_CONF"}) {
        const auto key = QString::fromLatin1(name);
        const auto saved = QStringLiteral("RUTOOL_DESKTOP_") + key;
        if (environment.contains(saved)) environment.insert(key, environment.value(saved));
        else environment.remove(key);
        environment.remove(saved);
    }
    for (const auto* name : {"APPIMAGE", "APPDIR", "ARGV0", "OWD", "APPIMAGE_EXTRACT_AND_RUN",
                             "RUTOOL_DESKTOP_ENV_SAVED", "RUTOOL_ORIGINAL_LD_PRELOAD"})
        environment.remove(QString::fromLatin1(name));
    return environment;
}

void ExternalLinks::open(const QUrl& url) {
    if (!url.isValid() || url.scheme() != QStringLiteral("https") || url.host().isEmpty() || !url.userInfo().isEmpty()) {
        emit failed(QStringLiteral("Некорректная внешняя HTTPS-ссылка."));
        return;
    }
#ifdef Q_OS_LINUX
    const auto environment = desktopEnvironment(QProcessEnvironment::systemEnvironment());
    const auto opener = QStandardPaths::findExecutable(QStringLiteral("xdg-open"),
        environment.value(QStringLiteral("PATH")).split(QLatin1Char(':'), Qt::SkipEmptyParts));
    if (opener.isEmpty()) {
        emit failed(QStringLiteral("Не найден xdg-open для открытия ссылки."));
        return;
    }
    QProcess process;
    process.setProcessEnvironment(environment);
    process.setProgram(opener);
    process.setArguments({url.toString(QUrl::FullyEncoded)});
    // xdg-open can remain alive with the browser. It must outlive RuTool.
    if (!process.startDetached())
        emit failed(QStringLiteral("Не удалось запустить xdg-open: %1").arg(process.errorString()));
#else
    if (!QDesktopServices::openUrl(url)) emit failed(QStringLiteral("Система не смогла открыть ссылку."));
#endif
}
