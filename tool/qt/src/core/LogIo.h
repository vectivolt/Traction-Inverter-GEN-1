// LogIo — telemetry logs on disk. Two formats, both readable by the replay transport and the analysis page:
//   JSONL: one telemetry object per line — the bridge's own "tel" lines (as recorded), or flat objects written for
//          sources without one ({"type":"tel","t_ms":..,"<channel>":value}); other line types are skipped;
//   CSV:   a header "t_ms,<channel>,..." and one row per frame; an empty cell is "no value", a cell that is not a
//          number is a text channel (quoted when it holds a comma, a quote or a newline).
#pragma once

#include "Telemetry.h"

#include <QFile>
#include <QString>
#include <QStringList>
#include <QVector>

namespace LogIo {
bool load(const QString &path, QVector<TelemetryFrame> &out, QString *err);
bool saveCsv(const QString &path, const QVector<TelemetryFrame> &frames, const QStringList &channels, QString *err);
QStringList channelsOf(const QVector<TelemetryFrame> &frames); // sorted union of numeric channels
} // namespace LogIo

// Appends frames to a JSONL file as they arrive (the session recorder).
class Recorder
{
public:
    bool open(const QString &path, QString *err);
    void write(const TelemetryFrame &f);
    void close();
    bool isOpen() const { return m_file.isOpen(); }
    QString path() const { return m_file.fileName(); }
    qint64 frames() const { return m_frames; }

private:
    QFile m_file;
    qint64 m_frames = 0;
};
