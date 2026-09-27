// PlotsPage — live plots over the telemetry store: a multi-trace time plot (pause, follow, zoom, cursors with a
// value/Δ readout, a window length and the simulator's telemetry rate), the id/iq plane with the current circle and
// the voltage ellipse, and the amplitude spectrum of any channel.
#pragma once

#include "Page.h"

#include <QTimer>

class DqPlot;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class SpectrumPlot;
class TimePlot;

class PlotsPage : public Page
{
    Q_OBJECT
public:
    explicit PlotsPage(Session &s, QWidget *parent = nullptr);
    void applyTheme() override;
    void selectChannels(const QStringList &channels); // presets, tests
    TimePlot *timePlot() const { return m_plot; }

protected:
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    void refresh();
    void rebuildTree();
    void applySelection();
    void updateCursorTable();
    void exportPng();
    void exportCsv();

    QTabWidget *m_tabs;
    TimePlot *m_plot;
    QTreeWidget *m_tree;
    QLineEdit *m_filter;
    QPushButton *m_pause, *m_follow, *m_cursors;
    QComboBox *m_window, *m_rate;
    QTableWidget *m_cursorTable;
    QLabel *m_cursorHead;
    DqPlot *m_dq;
    QSpinBox *m_dqSeconds;
    QLabel *m_dqNote;
    SpectrumPlot *m_fft;
    QComboBox *m_fftChannel, *m_fftSize;
    QCheckBox *m_fftLog, *m_fftAuto;
    QLabel *m_fftNote;
    QTimer m_timer;
    int m_knownChannels = -1;
    int m_tickCount = 0;
    QStringList m_selected;
};
