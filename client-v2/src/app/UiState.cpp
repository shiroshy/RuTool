#include "UiState.hpp"
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <algorithm>

QRect fitWindowPlacement(const QRect& saved, const QList<QRect>& screens, int preferred) {
    if (screens.isEmpty()) return {0, 0, 1100, 825};
    int target = std::clamp(preferred, 0, static_cast<int>(screens.size()) - 1);
    qint64 best{};
    for (qsizetype i = 0; i < screens.size(); ++i) {
        const auto overlap = saved.intersected(screens.at(i));
        const qint64 area = static_cast<qint64>(std::max(0, overlap.width())) * std::max(0, overlap.height());
        if (area > best) { best = area; target = static_cast<int>(i); }
    }
    const auto screen = screens.at(target);
    const QSize desired = saved.isValid() ? saved.size() : QSize{1100, 825};
    const int width = std::clamp(desired.width(), 940, std::max(940, screen.width()));
    const int height = std::clamp(desired.height(), 700, std::max(700, screen.height()));
    const QPoint origin = best > 0 ? saved.topLeft() : screen.topLeft() + QPoint{(screen.width() - width) / 2, (screen.height() - height) / 2};
    return {std::clamp(origin.x(), screen.left(), std::max(screen.left(), screen.right() - width + 1)),
            std::clamp(origin.y(), screen.top(), std::max(screen.top(), screen.bottom() - height + 1)), width, height};
}
UiState::UiState(QString settingsFile, bool persistent, QObject* parent)
    : QObject(parent), settings_(std::move(settingsFile), QSettings::IniFormat), persistent_(persistent) {
    dark_ = persistent_ && settings_.value(QStringLiteral("appearance/dark"), false).toBool();
    tracking_.setSingleShot(true);
    connect(&tracking_, &QTimer::timeout, this, &UiState::trackNormalGeometry);
}
void UiState::setDarkTheme(bool value) {
    if (dark_ == value) return;
    dark_ = value;
    if (persistent_) { settings_.setValue(QStringLiteral("appearance/dark"), dark_); settings_.sync(); }
    emit themeChanged();
}
bool UiState::hasAcceptedAgreement(const QString& hash) const {
    return !hash.isEmpty() && settings_.value(QStringLiteral("agreement/sha256")).toString() == hash;
}
void UiState::acceptAgreement(const QString& hash) {
    if (!persistent_ || hash.size() != 64) return;
    settings_.setValue(QStringLiteral("agreement/sha256"), hash);
    settings_.sync();
}
void UiState::trackNormalGeometry() {
    if (window_ && window_->windowState() == Qt::WindowNoState && window_->width() >= 940 && window_->height() >= 700)
        normal_ = window_->geometry();
}
void UiState::restoreWindow(QObject* object) {
    auto* window = qobject_cast<QWindow*>(object);
    if (!window || window_) return;
    window_ = window;
    const auto screens = QGuiApplication::screens();
    QList<QRect> areas;
    int preferred{};
    auto* underCursor = QGuiApplication::screenAt(QCursor::pos());
    for (qsizetype i = 0; i < screens.size(); ++i) {
        areas.append(screens.at(i)->availableGeometry());
        if (screens.at(i) == (underCursor ? underCursor : QGuiApplication::primaryScreen())) preferred = static_cast<int>(i);
    }
    const auto saved = persistent_ ? settings_.value(QStringLiteral("window/normalGeometry")).toRect() : QRect{};
    normal_ = fitWindowPlacement(saved, areas, preferred);
    for (auto* screen : screens) if (screen->availableGeometry().contains(normal_.center())) { window->setScreen(screen); break; }
    window->setGeometry(normal_);
    if (persistent_ && settings_.value(QStringLiteral("window/maximized"), false).toBool()) window->setWindowState(Qt::WindowMaximized);
    const auto changed = [this] { tracking_.start(0); };
    connect(window, &QWindow::xChanged, this, changed);
    connect(window, &QWindow::yChanged, this, changed);
    connect(window, &QWindow::widthChanged, this, changed);
    connect(window, &QWindow::heightChanged, this, changed);
}
void UiState::saveWindow() {
    if (!persistent_ || !window_) return;
    trackNormalGeometry();
    settings_.setValue(QStringLiteral("window/normalGeometry"), normal_);
    settings_.setValue(QStringLiteral("window/maximized"), window_->windowState() == Qt::WindowMaximized);
    settings_.sync();
}
