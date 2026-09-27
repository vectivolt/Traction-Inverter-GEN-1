#include "ReplayTransport.h"

#include "LogIo.h"

#include <QFileInfo>
#include <QJsonArray>

#include <algorithm>

ReplayTransport::ReplayTransport(QString path, QObject *parent) : ITransport(parent), m_path(std::move(path))
{
    m_timer.setInterval(5);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ReplayTransport::step);
}

QString ReplayTransport::name() const
{
    return QStringLiteral("Replay · %1 · ×%2%3")
        .arg(QFileInfo(m_path).fileName(), QString::number(m_factor), m_paused ? QStringLiteral(" · paused") : QString());
}

void ReplayTransport::open()
{
    setState(State::Opening, QStringLiteral("loading %1").arg(m_path));
    QString err;
    if (!LogIo::load(m_path, m_frames, &err)) {
        setState(State::Failed, QStringLiteral("%1: %2").arg(QFileInfo(m_path).fileName(), err));
        return;
    }
    QVector<double> dts;
    for (int i = 1; i < m_frames.size() && i < 2000; i++) {
        dts.push_back(m_frames[i].tMs - m_frames[i - 1].tMs);
    }
    if (!dts.isEmpty()) {
        std::nth_element(dts.begin(), dts.begin() + dts.size() / 2, dts.end());
        m_periodMs = dts[dts.size() / 2];
    }
    for (TelemetryFrame &f : m_frames) {
        f.source = QStringLiteral("replay");
    }
    seek(m_frames.first().tMs);
    m_wall.start();
    m_lastWall = 0;
    m_timer.start();
    setState(State::Open, name());
    QJsonObject o;
    o.insert(QLatin1String("type"), QLatin1String("replay"));
    o.insert(QLatin1String("path"), m_path);
    o.insert(QLatin1String("frames"), static_cast<int>(m_frames.size()));
    o.insert(QLatin1String("duration_ms"), endT() - startT());
    o.insert(QLatin1String("channels"), QJsonArray::fromStringList(LogIo::channelsOf(m_frames)));
    Q_EMIT info(o);
}

void ReplayTransport::close()
{
    m_timer.stop();
    setState(State::Closed);
}

QStringList ReplayTransport::commands() const
{
    return {QStringLiteral("pause"), QStringLiteral("time"), QStringLiteral("seek"), QStringLiteral("loop"),
            QStringLiteral("restart")};
}

quint64 ReplayTransport::send(const QString &cmd, const QJsonObject &args)
{
    const quint64 id = nextId();
    Q_EMIT traffic(QStringLiteral("tx"), cmd);
    if (cmd == QLatin1String("pause")) {
        m_paused = args.contains(QLatin1String("on")) ? args.value(QLatin1String("on")).toBool() : !m_paused;
        accept(id, cmd);
    } else if (cmd == QLatin1String("time")) {
        const double f = args.value(QLatin1String("factor")).toDouble(-1.0);
        if (f < 0.05 || f > 100.0) {
            refuse(id, cmd, QStringLiteral("factor in [0.05, 100]"));
            return id;
        }
        m_factor = f;
        accept(id, cmd);
    } else if (cmd == QLatin1String("seek")) {
        seek(std::clamp(args.value(QLatin1String("t_ms")).toDouble(startT()), startT(), endT()));
        accept(id, cmd);
    } else if (cmd == QLatin1String("loop")) {
        m_loop = args.value(QLatin1String("on")).toBool(true);
        accept(id, cmd);
    } else if (cmd == QLatin1String("restart")) {
        seek(startT());
        accept(id, cmd);
    } else {
        refuse(id, cmd, QStringLiteral("a replay is read-only: '%1' needs the simulator or a CAN bus").arg(cmd));
    }
    return id;
}

double ReplayTransport::expectedPeriodMs() const
{
    return (m_periodMs > 0.0) ? m_periodMs / m_factor : 0.0;
}

void ReplayTransport::seek(double t)
{
    m_playT = t;
    m_next = static_cast<int>(std::lower_bound(m_frames.cbegin(), m_frames.cend(), t,
                                               [](const TelemetryFrame &f, double x) { return f.tMs < x; }) -
                              m_frames.cbegin());
}

void ReplayTransport::step()
{
    const qint64 now = m_wall.elapsed();
    const qint64 dt = now - m_lastWall;
    m_lastWall = now;
    if (m_paused || m_frames.isEmpty()) {
        return;
    }
    m_playT += static_cast<double>(dt) * m_factor;
    int emitted = 0;
    while (m_next < m_frames.size() && m_frames[m_next].tMs <= m_playT && emitted < 2000) {
        Q_EMIT frame(m_frames[m_next]);
        m_next++;
        emitted++;
    }
    if (m_next >= m_frames.size()) {
        if (m_loop) {
            seek(startT());
        } else {
            m_playT = endT();
        }
    }
}
