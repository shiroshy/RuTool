#pragma once
#include <QObject>
#include <QSettings>
#include <QPointer>
#include <QWindow>
#include <QTimer>
#include <QList>

QRect fitWindowPlacement(const QRect& saved, const QList<QRect>& screens, int preferred = 0);

class UiState final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool darkTheme READ darkTheme WRITE setDarkTheme NOTIFY themeChanged)
public:
    explicit UiState(QString settingsFile, bool persistent = true, QObject* parent = nullptr);
    bool darkTheme() const { return dark_; }
    void setDarkTheme(bool value);
    Q_INVOKABLE void restoreWindow(QObject* object);
    Q_INVOKABLE bool hasAcceptedAgreement(const QString& hash) const;
    Q_INVOKABLE void acceptAgreement(const QString& hash);
    void saveWindow();
signals:
    void themeChanged();
private:
    void trackNormalGeometry();
    QSettings settings_;
    bool persistent_{};
    bool dark_{};
    QPointer<QWindow> window_;
    QRect normal_;
    QTimer tracking_;
};
