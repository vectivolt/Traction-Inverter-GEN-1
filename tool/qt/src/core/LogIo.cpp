#include "LogIo.h"

#include "CanCodec.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

#include <cmath>

namespace LogIo {
namespace {
bool loadJsonl(QFile &f, QVector<TelemetryFrame> &out, QString *err)
{
    int lineNo = 0;
    int bad = 0;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        lineNo++;
        if (line.isEmpty()) {
            continue;
        }
        QJsonParseError pe;
        const QJsonDocument d = QJsonDocument::fromJson(line, &pe);
        if (!d.isObject()) {
            bad++;
            continue;
        }
        const QJsonObject o = d.object();
        const QString type = o.value(QLatin1String("type")).toString(QStringLiteral("tel"));
        if (type != QLatin1String("tel") || !o.contains(QLatin1String("t_ms"))) {
            continue; // acks, logs, hello lines of a raw bridge capture
        }
        TelemetryFrame fr = Telemetry::fromJson(o, QStringLiteral("replay"));
        CanCodec::addChannelsFromHexText(fr); // a recorded simulator frame carries its INV_STATUS bytes
        out.push_back(std::move(fr));
    }
    if (out.isEmpty()) {
        *err = QStringLiteral("no telemetry lines (%1 lines read, %2 unparsable)").arg(lineNo).arg(bad);
        return false;
    }
    return true;
}

QStringList splitCsv(const QString &line)
{
    QStringList cells;
    QString cur;
    bool quoted = false;
    for (int i = 0; i < line.size(); i++) {
        const QChar c = line[i];
        if (quoted) {
            if (c == QLatin1Char('"') && i + 1 < line.size() && line[i + 1] == QLatin1Char('"')) {
                cur += QLatin1Char('"');
                i++;
            } else if (c == QLatin1Char('"')) {
                quoted = false;
            } else {
                cur += c;
            }
        } else if (c == QLatin1Char('"')) {
            quoted = true;
        } else if (c == QLatin1Char(',')) {
            cells << cur;
            cur.clear();
        } else {
            cur += c;
        }
    }
    cells << cur;
    return cells;
}

bool loadCsv(QFile &f, QVector<TelemetryFrame> &out, QString *err)
{
    QTextStream in(&f);
    const QStringList header = splitCsv(in.readLine().trimmed());
    const int tCol = static_cast<int>(header.indexOf(QStringLiteral("t_ms")));
    if (tCol < 0) {
        *err = QStringLiteral("CSV header has no t_ms column");
        return false;
    }
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const QStringList cells = splitCsv(line);
        TelemetryFrame fr;
        fr.source = QStringLiteral("replay");
        bool ok = false;
        fr.tMs = cells.value(tCol).toDouble(&ok);
        if (!ok) {
            continue;
        }
        for (int c = 0; c < header.size() && c < cells.size(); c++) {
            if (c == tCol || cells[c].isEmpty()) {
                continue;
            }
            bool isNum = false;
            const double v = cells[c].toDouble(&isNum);
            if (isNum) {
                fr.num.insert(header[c], v);
            } else if (cells[c] == QLatin1String("nan")) {
                fr.num.insert(header[c], TelemetryFrame::nan());
            } else {
                fr.text.insert(header[c], cells[c]);
            }
        }
        out.push_back(std::move(fr));
    }
    if (out.isEmpty()) {
        *err = QStringLiteral("no data rows");
        return false;
    }
    return true;
}
} // namespace

bool load(const QString &path, QVector<TelemetryFrame> &out, QString *err)
{
    out.clear();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *err = QStringLiteral("cannot open %1: %2").arg(path, f.errorString());
        return false;
    }
    const bool csv = path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive);
    if (!(csv ? loadCsv(f, out, err) : loadJsonl(f, out, err))) {
        return false;
    }
    // a log must be in time order for replay and integration; a reboot inside a capture keeps session time monotonic
    for (int i = 1; i < out.size(); i++) {
        if (out[i].tMs < out[i - 1].tMs) {
            *err = QStringLiteral("time goes backwards at frame %1 (t_ms %2 after %3)")
                       .arg(i).arg(out[i].tMs).arg(out[i - 1].tMs);
            return false;
        }
    }
    return true;
}

QStringList channelsOf(const QVector<TelemetryFrame> &frames)
{
    QSet<QString> s;
    for (const TelemetryFrame &f : frames) {
        for (auto it = f.num.begin(); it != f.num.end(); ++it) {
            s.insert(it.key());
        }
    }
    QStringList l(s.begin(), s.end());
    l.sort();
    return l;
}

bool saveCsv(const QString &path, const QVector<TelemetryFrame> &frames, const QStringList &channels, QString *err)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        *err = f.errorString();
        return false;
    }
    QTextStream out(&f);
    out << "t_ms";
    for (const QString &c : channels) {
        out << ',' << c;
    }
    out << '\n';
    for (const TelemetryFrame &fr : frames) {
        out << QString::number(fr.tMs, 'g', 12);
        for (const QString &c : channels) {
            out << ',';
            const auto it = fr.num.constFind(c);
            if (it != fr.num.constEnd()) {
                out << (std::isnan(it.value()) ? QStringLiteral("nan") : QString::number(it.value(), 'g', 9));
            } else if (fr.text.contains(c)) { // a text cell (loadCsv reads it back as text), quoted when it must be
                QString t = fr.text.value(c);
                if (t.contains(QLatin1Char(',')) || t.contains(QLatin1Char('"')) || t.contains(QLatin1Char('\n'))) {
                    t = QLatin1Char('"') + t.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"');
                }
                out << t;
            }
        }
        out << '\n';
    }
    out.flush();
    if (!f.commit()) {
        *err = f.errorString();
        return false;
    }
    return true;
}
} // namespace LogIo

bool Recorder::open(const QString &path, QString *err)
{
    close();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        *err = m_file.errorString();
        return false;
    }
    m_frames = 0;
    return true;
}

void Recorder::write(const TelemetryFrame &f)
{
    if (!m_file.isOpen()) {
        return;
    }
    QJsonObject o = Telemetry::toJson(f);
    o.insert(QLatin1String("type"), QLatin1String("tel"));
    m_file.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    m_file.write("\n");
    m_frames++;
}

void Recorder::close()
{
    if (m_file.isOpen()) {
        m_file.close();
    }
}
