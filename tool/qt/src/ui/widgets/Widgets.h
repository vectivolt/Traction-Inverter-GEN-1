// Widgets — the custom-painted pieces of the interface: cards, pills, big-number tiles with sparklines, the arc
// gauge, the banded bar (temperatures against their derating band), the state track of the §9 sequence, the
// checklist and the toast. Everything paints from Theme colours at paint time, so a theme switch applies at once,
// and every value-bearing widget has a stale state that greys the value and says how old it is.
#pragma once

#include "Theme.h"

#include <QFrame>
#include <QHash>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>

#include <cmath>

// A card: a titled surface with a body layout.
class Card : public QFrame
{
    Q_OBJECT
public:
    explicit Card(const QString &title, QWidget *parent = nullptr);
    QVBoxLayout *body() const { return m_body; }
    QLabel *titleLabel() const { return m_title; }
    void addHeaderWidget(QWidget *w); // right side of the title row

private:
    QVBoxLayout *m_body;
    QLabel *m_title;
    QHBoxLayout *m_header;
};

// A small rounded status label; severity drives its colours (theme.qss QLabel#pill[severity=...]).
QLabel *makePill(const QString &text, Theme::Sev s = Theme::Sev::Neutral, QWidget *parent = nullptr);
void setPill(QLabel *pill, const QString &text, Theme::Sev s);

// Formats a value with its decimals, or an em dash for NaN.
QString fmtNum(double v, int decimals);
QString fmtAge(qint64 ms);

// Big-number tile: a primary value (and optionally a second, e.g. torque applied vs commanded), unit, a sparkline of
// the recent values, a sub-text line, a severity accent and a stale badge.
class Tile : public QWidget
{
    Q_OBJECT
public:
    Tile(const QString &title, const QString &unit, int decimals, QWidget *parent = nullptr);
    void setValue(double v);                      // appends to the sparkline
    void setSecond(const QString &label, double v, const QString &primaryLabel = QString());
    void setSubText(const QString &s);
    void setSeverity(Theme::Sev s);
    void setStale(bool stale, qint64 ageMs);
    void clearHistory();
    double value() const { return m_value; }
    QSize sizeHint() const override { return {220, 132}; }
    QSize minimumSizeHint() const override { return {170, 118}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString m_title, m_unit, m_sub, m_label1, m_label2;
    int m_dec;
    double m_value = std::nan("");
    double m_second = std::nan("");
    bool m_hasSecond = false;
    QVector<double> m_h1, m_h2;
    Theme::Sev m_sev = Theme::Sev::Neutral;
    bool m_stale = true;
    qint64 m_age = -1;
};

// 240-degree arc gauge with coloured bands.
class ArcGauge : public QWidget
{
    Q_OBJECT
public:
    struct Band {
        double from, to;
        Theme::Sev sev;
    };
    ArcGauge(const QString &title, const QString &unit, double min, double max, int decimals, QWidget *parent = nullptr);
    void setValue(double v);
    void setRange(double min, double max);
    void setBands(const QVector<Band> &b);
    void setStale(bool s);
    QSize sizeHint() const override { return {200, 170}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString m_title, m_unit;
    double m_min, m_max, m_value = std::nan("");
    int m_dec;
    QVector<Band> m_bands;
    bool m_stale = true;
};

// Horizontal bar against a scale with zones: normal below warnFrom, warning to critFrom, critical beyond.
class BandBar : public QWidget
{
    Q_OBJECT
public:
    BandBar(const QString &label, const QString &unit, double min, double max, QWidget *parent = nullptr);
    void setValue(double v);
    void setZones(double warnFrom, double critFrom);
    void setStale(bool s);
    QSize sizeHint() const override { return {260, 30}; }
    QSize minimumSizeHint() const override { return {180, 28}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString m_label, m_unit;
    double m_min, m_max, m_value = std::nan(""), m_warn = std::nan(""), m_crit = std::nan("");
    bool m_stale = true;
};

// The §9 start-up sequence as a track (OFF ... RUN) with the side states (DERATE, FAULT, DISCHARGE, SAFE_POWERDOWN).
class StateTrack : public QWidget
{
    Q_OBJECT
public:
    explicit StateTrack(QWidget *parent = nullptr);
    void setState(int sm); // firmware enum value; -1 = unknown
    void setStale(bool s);
    QSize sizeHint() const override { return {760, 96}; }
    QSize minimumSizeHint() const override { return {560, 96}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    int m_state = -1;
    bool m_stale = true;
};

// A checklist of named conditions: true (met), false (not met) or unknown.
class CheckList : public QWidget
{
    Q_OBJECT
public:
    enum class State { Unknown, Ok, Fail };
    explicit CheckList(QWidget *parent = nullptr);
    void setItems(const QVector<QPair<QString, QString>> &items); // key -> "title|ref"
    void set(const QString &key, State s, const QString &detail = QString());
    void setAll(State s);
    void setStale(bool stale); // the last states, drawn greyed
    State state(const QString &key) const;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *e) override;

private:
    struct Item {
        QString key, title, ref, detail;
        State st = State::Unknown;
    };
    QVector<Item> m_items;
    bool m_stale = false;
    int rowAt(int y) const;
};

// Label/value rows in a grid; values are tabular numbers, coloured by severity, greyed when stale.
class KvGrid : public QWidget
{
    Q_OBJECT
public:
    explicit KvGrid(QWidget *parent = nullptr);
    // hostSide: the value is the host's own (a frame age), never greyed as stale
    void addRow(const QString &key, const QString &label, const QString &tip = QString(), bool hostSide = false);
    void setValue(const QString &key, const QString &text, Theme::Sev s = Theme::Sev::Neutral);
    void setStale(bool stale);
    QString valueText(const QString &key) const;

private:
    struct Row {
        QLabel *label, *value;
        Theme::Sev sev = Theme::Sev::Neutral;
        bool hostSide = false;
    };
    void paintRow(Row &r);
    QHash<QString, Row> m_rows;
    int m_n = 0;
    bool m_stale = true;
};

// A transient notification in a corner of its parent window.
class Toast : public QFrame
{
    Q_OBJECT
public:
    explicit Toast(QWidget *window);
    void show(const QString &title, const QString &text, Theme::Sev s, int ms = 5000);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void place();
    QLabel *m_title, *m_text;
    QTimer m_hide;
};
