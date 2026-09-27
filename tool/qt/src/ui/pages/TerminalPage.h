// TerminalPage — a command console over the active transport, with history (Up/Down), completion and a shorthand:
//   arm · torque 50 hold_ms=500 · inject desat_hs · uds 02 10 03 · param_set cal_torque_max_nm 300
//   or the raw protocol object: {"cmd":"speed","rpm":3000}
// Every command is answered by its acknowledgement (ok / refused + reason), matched by id.
#pragma once

#include "Page.h"

#include <QHash>

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;

class TerminalPage : public Page
{
    Q_OBJECT
public:
    explicit TerminalPage(Session &s, QWidget *parent = nullptr);
    // Parses one console line into a command and its arguments; false with *err on a syntax error.
    static bool parseLine(const QString &line, QString *cmd, QJsonObject *args, QString *err);
    void submit(const QString &line); // tests

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void print(const QString &text, const char *colorToken, const QString &prefix);
    void help();

    QPlainTextEdit *m_out;
    QLineEdit *m_in;
    QCheckBox *m_showRx, *m_showCan, *m_showEvents;
    QStringList m_history;
    int m_histPos = 0;
    QHash<quint64, QString> m_sent;
};
