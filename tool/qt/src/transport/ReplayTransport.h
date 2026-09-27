// ReplayTransport — plays a recorded log (LogIo: JSONL or CSV) with its own timing, scaled by a factor.
// Commands: pause {on}, time {factor}, seek {t_ms}, loop {on}, restart. Everything else is refused: a replay
// commands nothing.
#pragma once

#include "ITransport.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QVector>

class ReplayTransport : public ITransport
{
    Q_OBJECT
public:
    explicit ReplayTransport(QString path, QObject *parent = nullptr);

    Kind kind() const override { return Kind::Replay; }
    QString name() const override;
    void open() override;
    void close() override;
    bool isReplay() const override { return true; }
    QStringList commands() const override;
    quint64 send(const QString &cmd, const QJsonObject &args = {}) override;
    double expectedPeriodMs() const override;

    double position() const { return m_playT; } // log time of the playhead
    double startT() const { return m_frames.isEmpty() ? 0.0 : m_frames.first().tMs; }
    double endT() const { return m_frames.isEmpty() ? 0.0 : m_frames.last().tMs; }
    int frameCount() const { return static_cast<int>(m_frames.size()); }
    bool finished() const { return !m_frames.isEmpty() && m_next >= m_frames.size() && !m_loop; }

private:
    void step();
    void seek(double t);

    QString m_path;
    QVector<TelemetryFrame> m_frames;
    QTimer m_timer;
    QElapsedTimer m_wall;
    qint64 m_lastWall = 0;
    double m_playT = 0.0;
    double m_factor = 1.0;
    double m_periodMs = 0.0;
    int m_next = 0;
    bool m_paused = false;
    bool m_loop = false;
};
