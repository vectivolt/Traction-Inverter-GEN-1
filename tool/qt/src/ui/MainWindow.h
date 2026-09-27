// MainWindow — the frame around the pages: the connection bar (link state, transport, heartbeat age, frame rate,
// recording, service lock) visible on every page, the left navigation (Ctrl+1 … Ctrl+9), menus with every action on a
// shortcut, toasts for alerts and new DTCs, and the dark/light theme.
#pragma once

#include "BridgeTransport.h"
#include "CanTransport.h"
#include "Session.h"

#include <QMainWindow>

class QComboBox;
class QLabel;
class QListWidget;
class QStackedWidget;
class QToolButton;
class Page;
class Toast;
class ControlsPage;
class FaultsPage;
class PlotsPage;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    Session &session() { return m_s; }
    void connectSimulator(const BridgeTransport::Options &o);
    bool openReplay(const QString &path);
    bool connectCan(const CanTransport::Options &o, QString *err);
    void showPage(int index);
    int pageCount() const;
    Page *page(int index) const;
    ControlsPage *controls() const { return m_controls; }
    FaultsPage *faults() const { return m_faults; }
    void setTheme(bool light); // the whole window, Qwt plots included
    PlotsPage *plots() const { return m_plots; }

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void buildMenus();
    void buildConnectionBar(QWidget *host);
    void updateConnectionBar();
    void toggleRecording();
    void serviceKey();

    Session m_s;
    QListWidget *m_nav;
    QStackedWidget *m_stack;
    QVector<Page *> m_pages;
    ControlsPage *m_controls = nullptr;
    FaultsPage *m_faults = nullptr;
    PlotsPage *m_plots = nullptr;
    QLabel *m_linkPill, *m_linkName, *m_age, *m_recPill;
    QToolButton *m_connectBtn, *m_disconnectBtn, *m_recordBtn, *m_lockBtn, *m_pauseBtn;
    QComboBox *m_speed; // simulation / replay time factor
    const ITransport *m_shown = nullptr;
    Toast *m_toast;
    QAction *m_lightAction;
};
