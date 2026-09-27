// Page — the base of every page in the left navigation: it holds the Session, builds the standard header, and
// receives the theme change (Qwt plots repaint their colours).
#pragma once

#include "Session.h"
#include "Theme.h"

#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

class Page : public QWidget
{
    Q_OBJECT
public:
    Page(Session &s, const QString &title, const QString &subtitle, QWidget *parent = nullptr);
    virtual void applyTheme() {}
    QString title() const { return m_title; }

protected:
    // The content area below the header; scrollable pages put their content in a scroll area.
    QVBoxLayout *content() const { return m_content; }
    QVBoxLayout *scrollContent(); // turns the content area into a vertical scroll area (call once)
    // Renders the newest frame at most 30 times a second (telemetry may come at 500 Hz; a label need not)
    void onFrames(std::function<void(const TelemetryFrame &)> render);
    // A banner under the header naming the image verifier's root of trust (info "root": the bridge's hello, or DID
    // 0xFD23 over CAN): a TEST key or none prominently, a release key with its id; hidden while unknown.
    class QLabel *addRootBanner();
    static QString rootText(const QJsonObject &root, Theme::Sev *sev = nullptr);
    Session &m_s;

private:
    QString m_title;
    QVBoxLayout *m_content;
    class QLabel *m_stale;
    std::function<void(const TelemetryFrame &)> m_render;
    QTimer m_frameTimer;
    bool m_pending = false;
};
