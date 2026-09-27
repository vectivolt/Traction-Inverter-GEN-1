// AnalysisPage — offline analysis of a recorded log (JSONL/CSV) or of the session buffer: per-channel statistics,
// a time plot with event markers (state changes, DTC raise/clear), energy and efficiency integration, temperature
// against load, and CSV/PNG export.
#pragma once

#include "Page.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTableWidget;
class QTabWidget;
class QwtPlot;
class QwtPlotCurve;
class TimePlot;
class KvGrid;

class AnalysisPage : public Page
{
    Q_OBJECT
public:
    explicit AnalysisPage(Session &s, QWidget *parent = nullptr);
    void applyTheme() override;
    bool loadFile(const QString &path, QString *err); // tests
    int channelCount() const;

private:
    void useFrames(QVector<TelemetryFrame> frames, const QString &source);
    void rebuildStats();
    void plotSelection();
    void computeEnergy();
    void computeScatter();
    void exportCsv();
    void exportPng();

    QVector<TelemetryFrame> m_frames;
    TelemetryStore m_store{16};
    QLabel *m_source;
    QTabWidget *m_tabs;
    QLineEdit *m_filter;
    QTableWidget *m_stats;
    TimePlot *m_plot;
    QListWidget *m_events;
    QComboBox *m_pDc, *m_pMech;
    KvGrid *m_energy;
    QComboBox *m_scX, *m_scY;
    QwtPlot *m_scatter;
    QwtPlotCurve *m_scatterCurve, *m_fitCurve;
    QLabel *m_scNote;
};
