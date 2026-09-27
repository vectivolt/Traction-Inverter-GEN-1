#include "TerminalPage.h"

#include "Widgets.h"

#include <QCheckBox>
#include <QCompleter>
#include <QDateTime>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStringListModel>

namespace {
// the argument a lone positional value fills, per command (tool/PROTOCOL.md A.4)
QString positionalKey(const QString &cmd)
{
    static const QHash<QString, QString> k = {
        {QStringLiteral("torque"), QStringLiteral("nm")},      {QStringLiteral("speed"), QStringLiteral("rpm")},
        {QStringLiteral("gear"), QStringLiteral("gear")},      {QStringLiteral("inject"), QStringLiteral("fault")},
        {QStringLiteral("clear"), QStringLiteral("fault")},    {QStringLiteral("rate"), QStringLiteral("hz")},
        {QStringLiteral("time"), QStringLiteral("factor")},    {QStringLiteral("coolant"), QStringLiteral("c")},
        {QStringLiteral("vspeed"), QStringLiteral("kmh")},
        {QStringLiteral("step"), QStringLiteral("ms")},        {QStringLiteral("key"), QStringLiteral("on")},
        {QStringLiteral("pause"), QStringLiteral("on")},       {QStringLiteral("enable"), QStringLiteral("on")},
        {QStringLiteral("asc"), QStringLiteral("on")},         {QStringLiteral("can_tap"), QStringLiteral("on")},
        {QStringLiteral("vcu"), QStringLiteral("on")},         {QStringLiteral("contactors"), QStringLiteral("state")},
        {QStringLiteral("param_get"), QStringLiteral("name")}, {QStringLiteral("seek"), QStringLiteral("t_ms")},
        {QStringLiteral("loop"), QStringLiteral("on")},
    };
    return k.value(cmd);
}

QJsonValue scalar(const QString &v)
{
    if (v == QLatin1String("true") || v == QLatin1String("on")) {
        return true;
    }
    if (v == QLatin1String("false") || v == QLatin1String("off")) {
        return false;
    }
    bool ok = false;
    const double d = v.toDouble(&ok);
    if (ok) {
        return d;
    }
    if (v.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        const qlonglong x = v.mid(2).toLongLong(&ok, 16);
        if (ok) {
            return static_cast<double>(x);
        }
    }
    QString s = v;
    if (s.size() >= 2 && s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"'))) {
        s = s.mid(1, s.size() - 2);
    }
    return s;
}

QStringList tokens(const QString &line)
{
    QStringList out;
    static const QRegularExpression re(QStringLiteral(R"((\S+="[^"]*"|"[^"]*"|\S+))"));
    auto it = re.globalMatch(line);
    while (it.hasNext()) {
        out << it.next().captured(1);
    }
    return out;
}

const char *const SYNOPSIS[][2] = {
    {"arm", "start the arm sequence (enable, precharge, contactors)"},
    {"disarm", "torque 0, enable off, open the contactors below n_x"},
    {"torque <nm> [hold_ms=] [slew_nm_s=]", "the VCU torque request (hold_ms: wall-clock deadman)"},
    {"speed <rpm> [ramp_rpm_s=]", "the dyno speed (simulator)"},
    {"gear N|D|R|P", "the gear in VCU_CMD"},
    {"inject <fault> / clear <fault|all>", "desat_hs desat_ls overcurrent overtemp vdc_sense_loss battery_loss resolver_loss can_loss hvil_open v5gd_loss lv_overvoltage"},
    {"bms chg_kw= dis_kw= pack_v=", "BMS limits relayed in VCU_BMS"},
    {"fault_reset / retry_auth / discharge", "VCU bits: 100 ms / 3 s / 6 s"},
    {"param_get <name> / param_set <name> <value> / params / param_reset", "the cal_* table"},
    {"uds <hex>", "one ISO 15765-2 single frame to 0x7E1, e.g. uds 02 27 01"},
    {"rate <hz> / time <factor> / pause [on] / step <ms>", "simulation pacing"},
    {"key on|off / reboot / provision val= route= otp= cal=", "power and EOL provisioning (simulator)"},
    {"asc on|off", "simulator test hook"},
    {"dtc_clear", "the firmware's UDS 0x14 (SecurityAccess; HV absent; disarmed; latched DESAT DTCs are kept)"},
    {"vcu on|off / contactors <state> / send can_id= hex= / status", "CAN bench (service key)"},
    {"help / clear", "this list / empty the console"},
};
} // namespace

TerminalPage::TerminalPage(Session &s, QWidget *parent)
    : Page(s, QStringLiteral("Terminal"),
           QStringLiteral("Commands over the active transport. Shorthand: `torque 50 hold_ms=500`, `uds 02 10 03`, or a raw "
                          "protocol object `{\"cmd\":\"speed\",\"rpm\":3000}`. Up/Down: history, Tab: complete."),
           parent)
{
    auto *bar = new QHBoxLayout;
    m_showRx = new QCheckBox(QStringLiteral("Bridge lines"));
    m_showRx->setChecked(true);
    m_showRx->setToolTip(QStringLiteral("hello, params and other non-telemetry lines as received"));
    m_showCan = new QCheckBox(QStringLiteral("CAN frames"));
    m_showCan->setToolTip(QStringLiteral("can_tap lines (simulator) and non-periodic frames (CAN)"));
    m_showEvents = new QCheckBox(QStringLiteral("Session events"));
    m_showEvents->setChecked(true);
    auto *clear = new QPushButton(QStringLiteral("Clear"));
    bar->addWidget(m_showRx);
    bar->addWidget(m_showCan);
    bar->addWidget(m_showEvents);
    bar->addStretch(1);
    bar->addWidget(clear);
    content()->addLayout(bar);
    m_out = new QPlainTextEdit;
    m_out->setObjectName(QStringLiteral("terminalOutput"));
    m_out->setReadOnly(true);
    m_out->setMaximumBlockCount(5000);
    m_out->setFont(Theme::monoFont(12));
    m_out->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    content()->addWidget(m_out, 1);
    m_in = new QLineEdit;
    m_in->setObjectName(QStringLiteral("terminalInput"));
    m_in->setFont(Theme::monoFont(13));
    m_in->setPlaceholderText(QStringLiteral("›  type a command — help lists them"));
    m_in->installEventFilter(this);
    auto *model = new QStringListModel(this);
    auto *comp = new QCompleter(model, this);
    comp->setCaseSensitivity(Qt::CaseInsensitive);
    comp->setCompletionMode(QCompleter::InlineCompletion);
    m_in->setCompleter(comp);
    content()->addWidget(m_in);

    connect(clear, &QPushButton::clicked, m_out, &QPlainTextEdit::clear);
    connect(m_in, &QLineEdit::returnPressed, this, [this] {
        const QString line = m_in->text();
        m_in->clear();
        submit(line);
    });
    connect(&m_s, &Session::transportChanged, this, [this, model] {
        QStringList cmds = m_s.transport() ? m_s.transport()->commands() : QStringList();
        cmds << QStringLiteral("help") << QStringLiteral("clear");
        model->setStringList(cmds);
        print(m_s.transport() ? QStringLiteral("— %1 —").arg(m_s.transport()->name()) : QStringLiteral("— disconnected —"), "dim", QString());
    });
    connect(&m_s, &Session::ackReceived, this, [this](const Ack &a) {
        const QString sent = m_sent.take(a.id);
        if (sent.isEmpty() && a.ok) {
            return; // another page's command (e.g. the deadman's 10 Hz torque refresh): only its refusals are news
        }
        QString extra;
        for (const char *k : {"value", "violations", "rsp", "msg", "frames", "name", "ptype", "min", "max", "default"}) {
            if (a.raw.contains(QLatin1String(k))) {
                extra += QStringLiteral(" %1=%2").arg(QLatin1String(k), a.raw.value(QLatin1String(k)).toVariant().toString());
            }
        }
        print(QStringLiteral("%1 %2%3%4").arg(a.ok ? QStringLiteral("ok") : QStringLiteral("REFUSED"), a.cmd,
                                              a.message.isEmpty() ? QString() : QStringLiteral(": ") + a.message, extra),
              a.ok ? "ok" : "crit", QStringLiteral("‹ "));
    });
    connect(&m_s, &Session::traffic, this, [this](const QString &dir, const QString &line) {
        if (dir == QLatin1String("can") && m_showCan->isChecked()) {
            print(line, "info", QStringLiteral("⇄ "));
        } else if (dir == QLatin1String("rx") && m_showRx->isChecked()) {
            print(line.size() > 400 ? line.left(400) + QStringLiteral(" …") : line, "muted", QStringLiteral("‹ "));
        }
    });
    connect(&m_s, &Session::eventLogged, this, [this](const LogEntry &e) {
        if (m_showEvents->isChecked() && e.level != QLatin1String("tx")) {
            const char *tok = (e.level == QLatin1String("error") || e.level == QLatin1String("dtc")) ? "crit"
                            : (e.level == QLatin1String("warn") || e.level == QLatin1String("alert")) ? "warn" : "dim";
            print(QStringLiteral("[%1] %2").arg(e.level, e.text), tok, QStringLiteral("• "));
        }
    });
    print(QStringLiteral("Traction Tool terminal — type help"), "dim", QString());
}

bool TerminalPage::parseLine(const QString &line, QString *cmd, QJsonObject *args, QString *err)
{
    const QString l = line.trimmed();
    *args = QJsonObject();
    if (l.startsWith(QLatin1Char('{'))) {
        QJsonParseError pe;
        const QJsonDocument d = QJsonDocument::fromJson(l.toUtf8(), &pe);
        if (!d.isObject()) {
            *err = pe.errorString();
            return false;
        }
        *args = d.object();
        *cmd = args->take(QLatin1String("cmd")).toString();
        args->remove(QLatin1String("id"));
        if (cmd->isEmpty()) {
            *err = QStringLiteral("the object has no \"cmd\"");
            return false;
        }
        return true;
    }
    QStringList t = tokens(l);
    if (t.isEmpty()) {
        *err = QStringLiteral("empty");
        return false;
    }
    *cmd = t.takeFirst();
    QStringList positional;
    for (const QString &tok : t) {
        const int eq = static_cast<int>(tok.indexOf(QLatin1Char('=')));
        if (eq > 0) {
            args->insert(tok.left(eq), scalar(tok.mid(eq + 1)));
        } else {
            positional << tok;
        }
    }
    if (positional.isEmpty()) {
        return true;
    }
    if (*cmd == QLatin1String("uds") || *cmd == QLatin1String("can_rx") || *cmd == QLatin1String("send")) {
        if ((*cmd != QLatin1String("uds")) && !args->contains(QLatin1String("can_id"))) {
            args->insert(QStringLiteral("can_id"), scalar(positional.takeFirst()));
        }
        args->insert(QStringLiteral("hex"), positional.join(QLatin1Char(' ')));
    } else if (*cmd == QLatin1String("param_set") && positional.size() == 2) {
        args->insert(QStringLiteral("name"), positional[0]);
        args->insert(QStringLiteral("value"), scalar(positional[1]));
    } else if (positional.size() == 1 && !positionalKey(*cmd).isEmpty()) {
        const QString k = positionalKey(*cmd);
        const QJsonValue v = scalar(positional[0]);
        args->insert(k, (k == QLatin1String("gear") || k == QLatin1String("fault") || k == QLatin1String("state") ||
                         k == QLatin1String("name")) ? QJsonValue(positional[0]) : v);
    } else {
        *err = QStringLiteral("unexpected '%1' — use key=value").arg(positional.join(QLatin1Char(' ')));
        return false;
    }
    return true;
}

void TerminalPage::submit(const QString &line)
{
    const QString l = line.trimmed();
    if (l.isEmpty()) {
        return;
    }
    if (m_history.isEmpty() || m_history.last() != l) {
        m_history << l;
    }
    m_histPos = static_cast<int>(m_history.size());
    print(l, "accent", QStringLiteral("› "));
    if (l == QLatin1String("help")) {
        help();
        return;
    }
    if (l == QLatin1String("clear")) {
        m_out->clear();
        return;
    }
    QString cmd, err;
    QJsonObject args;
    if (!parseLine(l, &cmd, &args, &err)) {
        print(err, "crit", QStringLiteral("! "));
        return;
    }
    if (!m_s.transport()) {
        print(QStringLiteral("not connected"), "crit", QStringLiteral("! "));
        return;
    }
    const quint64 id = m_s.command(cmd, args);
    if (id) {
        m_sent.insert(id, cmd);
    }
}

void TerminalPage::help()
{
    for (const auto &s : SYNOPSIS) {
        print(QStringLiteral("%1\n      %2").arg(QString::fromUtf8(s[0]), QString::fromUtf8(s[1])), "muted", QStringLiteral("  "));
    }
    if (m_s.transport()) {
        print(QStringLiteral("this transport takes: %1").arg(m_s.transport()->commands().join(QStringLiteral(", "))), "dim", QStringLiteral("  "));
    }
}

void TerminalPage::print(const QString &text, const char *colorToken, const QString &prefix)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    m_out->appendHtml(QStringLiteral("<span style='color:%1'>%2</span> <span style='color:%3;white-space:pre-wrap'>%4%5</span>")
                          .arg(Theme::color("dim").name(), ts, Theme::color(colorToken).name(), prefix.toHtmlEscaped(),
                               text.toHtmlEscaped()));
}

bool TerminalPage::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_in && e->type() == QEvent::KeyPress) {
        auto *k = static_cast<QKeyEvent *>(e);
        if (k->key() == Qt::Key_Up && !m_history.isEmpty()) {
            m_histPos = std::max(0, m_histPos - 1);
            m_in->setText(m_history.value(m_histPos));
            return true;
        }
        if (k->key() == Qt::Key_Down && !m_history.isEmpty()) {
            m_histPos = std::min(static_cast<int>(m_history.size()), m_histPos + 1);
            m_in->setText(m_history.value(m_histPos));
            return true;
        }
    }
    return Page::eventFilter(o, e);
}
