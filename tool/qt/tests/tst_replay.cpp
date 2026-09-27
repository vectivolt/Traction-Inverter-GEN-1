// Logs and replay: JSONL (raw bridge lines, with the INV_STATUS hex decoded on load) and CSV round trips, the
// recorder, rejection of a log that goes back in time, and ReplayTransport timing (real, scaled, paused, seek) and
// its read-only refusals; and the FW-46 ripple-map CSV the Commissioning page imports.
#include "CommissioningSequencer.h"
#include "Dsp.h"
#include "LogIo.h"
#include "ReplayTransport.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
void writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}
QByteArray jsonl(int n, double dt)
{
    QByteArray out = R"({"type":"hello","fw_id":"0x0A0F0014"})" "\n";
    for (int i = 0; i < n; i++) {
        out += QStringLiteral(R"({"type":"tel","t_ms":%1,"src":"sim","motion":{"speed_rpm":%2},"link":{"vdc_v":750}})")
                   .arg(1000.0 + i * dt).arg(i * 10.0).toUtf8();
        out += "\n";
        if (i == 3) {
            out += R"({"type":"ack","id":1,"cmd":"arm","ok":true,"t_ms":1003})" "\n";
        }
    }
    return out;
}
} // namespace

class TstReplay : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void jsonlLoad();
    void csvRoundTrip();
    void recorderRoundTrip();
    void rejectsBackwardsTime();
    void realTimePlayback();
    void scaledPausedSeek();
    void readOnly();
    void analysisMath();
    void rippleCsv();

private:
    QTemporaryDir m_dir;
};

void TstReplay::jsonlLoad()
{
    const QString p = m_dir.filePath(QStringLiteral("a.jsonl"));
    QByteArray data = jsonl(5, 10);
    // a recorded simulator frame with its INV_STATUS bytes: decoded on load like live telemetry
    data += R"({"type":"tel","t_ms":1100,"can":"9A13078ADC05B80B3E1D5F0128000000F3050000"})" "\n";
    writeFile(p, data);
    QVector<TelemetryFrame> frames;
    QString err;
    QVERIFY2(LogIo::load(p, frames, &err), qPrintable(err));
    QCOMPARE(frames.size(), 6); // hello and ack lines skipped
    QCOMPARE(frames[2].v(QStringLiteral("motion.speed_rpm")), 20.0);
    QCOMPARE(frames[5].v(QStringLiteral("inv.torque_applied_nm")), 150.0);
    QVERIFY(LogIo::channelsOf(frames).contains(QStringLiteral("link.vdc_v")));
}

void TstReplay::csvRoundTrip()
{
    QVector<TelemetryFrame> in;
    for (int i = 0; i < 4; i++) {
        TelemetryFrame f;
        f.tMs = i * 5.0;
        f.num.insert(QStringLiteral("a"), i * 1.5);
        if (i != 2) {
            f.num.insert(QStringLiteral("b"), -i);
        }
        f.num.insert(QStringLiteral("c"), std::nan(""));
        f.text.insert(QStringLiteral("d"), (i == 1) ? QStringLiteral("x, \"y\"") : QStringLiteral("z")); // a text cell
        in.push_back(f);
    }
    const QString p = m_dir.filePath(QStringLiteral("x.csv"));
    QString err;
    QVERIFY(LogIo::saveCsv(p, in, {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")}, &err));
    QVector<TelemetryFrame> out;
    QVERIFY2(LogIo::load(p, out, &err), qPrintable(err));
    QCOMPARE(out.size(), 4);
    QCOMPARE(out[3].tMs, 15.0);
    QCOMPARE(out[3].v(QStringLiteral("a")), 4.5);
    QVERIFY(!out[2].has(QStringLiteral("b"))); // an empty cell is "no value"
    QCOMPARE(out[1].v(QStringLiteral("b")), -1.0);
    QVERIFY(out[0].has(QStringLiteral("c")) && std::isnan(out[0].v(QStringLiteral("c"))));
    QCOMPARE(out[1].str(QStringLiteral("d")), QStringLiteral("x, \"y\"")); // quoted on the way out, unquoted on the way in
    QCOMPARE(out[3].str(QStringLiteral("d")), QStringLiteral("z"));
}

void TstReplay::recorderRoundTrip()
{
    const QString p = m_dir.filePath(QStringLiteral("rec.jsonl"));
    Recorder r;
    QString err;
    QVERIFY(r.open(p, &err));
    TelemetryFrame f; // a CAN frame: no raw object, written flat
    f.source = QStringLiteral("can");
    f.num.insert(QStringLiteral("inv.speed_rpm"), 1200);
    for (int i = 0; i < 3; i++) {
        f.tMs = i * 10.0;
        r.write(f);
    }
    r.close();
    QCOMPARE(r.frames(), qint64(3));
    QVector<TelemetryFrame> out;
    QVERIFY2(LogIo::load(p, out, &err), qPrintable(err));
    QCOMPARE(out.size(), 3);
    QCOMPARE(out[2].v(QStringLiteral("inv.speed_rpm")), 1200.0);
}

void TstReplay::rejectsBackwardsTime()
{
    const QString p = m_dir.filePath(QStringLiteral("back.jsonl"));
    writeFile(p, R"({"type":"tel","t_ms":10})" "\n" R"({"type":"tel","t_ms":5})" "\n");
    QVector<TelemetryFrame> out;
    QString err;
    QVERIFY(!LogIo::load(p, out, &err));
    QVERIFY(err.contains(QLatin1String("backwards")));
    writeFile(p, "garbage\n");
    QVERIFY(!LogIo::load(p, out, &err));
}

void TstReplay::realTimePlayback()
{
    const QString p = m_dir.filePath(QStringLiteral("rt.jsonl"));
    writeFile(p, jsonl(31, 10)); // 300 ms of log
    ReplayTransport t(p);
    QSignalSpy frames(&t, &ITransport::frame);
    QElapsedTimer clock;
    clock.start();
    t.open();
    QCOMPARE(t.state(), ITransport::State::Open);
    QTRY_COMPARE_WITH_TIMEOUT(frames.size(), 31, 3000);
    const qint64 took = clock.elapsed();
    QVERIFY2(took >= 250 && took < 1500, qPrintable(QString::number(took))); // real timing, not instant
    QCOMPARE(frames.at(0).at(0).value<TelemetryFrame>().source, QStringLiteral("replay"));
    QVERIFY(t.finished());
    QCOMPARE(t.expectedPeriodMs(), 10.0);
}

void TstReplay::scaledPausedSeek()
{
    const QString p = m_dir.filePath(QStringLiteral("scaled.jsonl"));
    writeFile(p, jsonl(101, 10)); // 1 s of log
    ReplayTransport t(p);
    QSignalSpy frames(&t, &ITransport::frame);
    QSignalSpy acks(&t, &ITransport::ack);
    t.send(QStringLiteral("time"), {{QStringLiteral("factor"), 10.0}});
    t.open();
    QElapsedTimer clock;
    clock.start();
    QTRY_COMPARE_WITH_TIMEOUT(frames.size(), 101, 3000);
    QVERIFY2(clock.elapsed() < 600, qPrintable(QString::number(clock.elapsed()))); // ×10: ~100 ms
    t.send(QStringLiteral("seek"), {{QStringLiteral("t_ms"), 1500.0}});
    QCOMPARE(t.position(), 1500.0);
    t.send(QStringLiteral("pause"), {{QStringLiteral("on"), true}});
    frames.clear();
    t.send(QStringLiteral("restart"));
    QTest::qWait(150);
    QCOMPARE(frames.size(), 0); // paused
    t.send(QStringLiteral("pause"), {{QStringLiteral("on"), false}});
    QTRY_VERIFY_WITH_TIMEOUT(frames.size() > 10, 2000);
    for (const auto &a : acks) {
        QVERIFY(a.at(0).value<Ack>().ok);
    }
    t.send(QStringLiteral("time"), {{QStringLiteral("factor"), 0.0}});
    QVERIFY(!acks.last().at(0).value<Ack>().ok);
}

void TstReplay::readOnly()
{
    const QString p = m_dir.filePath(QStringLiteral("ro.jsonl"));
    writeFile(p, jsonl(3, 10));
    ReplayTransport t(p);
    QSignalSpy acks(&t, &ITransport::ack);
    t.open();
    for (const char *c : {"arm", "torque", "inject", "param_set"}) {
        t.send(QLatin1String(c));
    }
    QCOMPARE(acks.size(), 4);
    for (const auto &a : acks) {
        QVERIFY(!a.at(0).value<Ack>().ok);
    }
    ReplayTransport missing(m_dir.filePath(QStringLiteral("nope.jsonl")));
    missing.open();
    QCOMPARE(missing.state(), ITransport::State::Failed);
}

void TstReplay::analysisMath()
{
    // a 50 Hz sine of amplitude 2 plus an offset, sampled at 1 kHz: the spectrum peaks at 50 Hz with amplitude 2
    QVector<double> x;
    for (int i = 0; i < 4096; i++) {
        x.push_back(3.0 + 2.0 * std::sin(2.0 * 3.14159265358979 * 50.0 * i / 1000.0));
    }
    QVector<double> f, a;
    QVERIFY(Dsp::spectrum(x, 1000.0, f, a));
    int pk = 1;
    for (int k = 1; k < a.size(); k++) {
        pk = (a[k] > a[pk]) ? k : pk;
    }
    QVERIFY2(std::abs(f[pk] - 50.0) <= f[1], qPrintable(QString::number(f[pk])));
    QVERIFY2(std::abs(a[pk] - 2.0) < 0.1, qPrintable(QString::number(a[pk])));
    QVERIFY(a[0] < 0.01); // the DC is removed before the window
    QVERIFY(!Dsp::spectrum(QVector<double>(8, 1.0), 1000.0, f, a));
    // statistics skip non-finite samples
    const Dsp::Stats st = Dsp::stats({1.0, 2.0, std::nan(""), 3.0});
    QCOMPARE(st.n, 3);
    QCOMPARE(st.mean, 2.0);
    QCOMPARE(st.min, 1.0);
    QCOMPARE(st.max, 3.0);
    QVERIFY(std::abs(st.rms - std::sqrt(14.0 / 3.0)) < 1e-12);
    // 10 kW for 10 s motoring, then 5 kW regen for 4 s: 100 kJ in, 20 kJ back; a NaN gap is not bridged
    QVector<double> t, p;
    for (int i = 0; i <= 140; i++) {
        t.push_back(i * 100.0);
        p.push_back(i <= 100 ? 10000.0 : -5000.0);
    }
    const double inJ = Dsp::integratePart(t, p, true), outJ = Dsp::integratePart(t, p, false);
    // the sign change between samples 100 and 101 is split at the samples: +500 J one side, -250 J the other
    QVERIFY2(std::abs(inJ - 100500.0) < 1.0, qPrintable(QString::number(inJ)));
    QVERIFY2(std::abs(outJ + 19750.0) < 1.0, qPrintable(QString::number(outJ)));
    p[50] = std::nan("");
    QVERIFY(std::abs(Dsp::integratePart(t, p, true) - (100500.0 - 2000.0)) < 1.0);
}

// FW-46: (electrical angle °, torque ripple N·m) resampled to 36 points, the mean removed, i_q = −(T − mean)/(1.5 pp ψ)
// in 0.01 A; a header, comments and any separator; the angle wrapped; refused: a gap over 10° el, two samples at one
// angle, a bad row after the data, beyond ±30 A, no ψ.
void TstReplay::rippleCsv()
{
    using Seq = CommissioningSequencer;
    const double kt = 1.5 * 4 * 0.15; // the screening motor: 0.9 N·m/A
    QByteArray csv = "# a comment\nangle_deg,torque_nm\n";
    QByteArray wrapped;
    for (int d = 0; d < 360; d += 5) { // 1 + 0.5 sin θ: the mean 1 N·m removed
        const double t = 1.0 + 0.5 * std::sin(qDegreesToRadians(static_cast<double>(d)));
        csv += QByteArray::number(d) + (d % 2 ? ";\t" : " , ") + QByteArray::number(t, 'f', 6) + "\n";
        wrapped += QByteArray::number(d >= 180 ? d - 360 : d) + "," + QByteArray::number(t, 'f', 6) + "\n";
    }
    const Seq::Ripple r = Seq::rippleFromCsv(csv, 0.15, 4);
    QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
    QCOMPARE(r.rows, 72);
    QCOMPARE(r.table.size(), Seq::RIPPLE_N);
    QVERIFY(std::abs(r.meanNm - 1.0) < 1e-6);
    for (int k = 0; k < Seq::RIPPLE_N; k++) {
        QCOMPARE(int(r.table[k]), int(std::lround(-100.0 * 0.5 * std::sin(qDegreesToRadians(k * 10.0)) / kt)));
    }
    QCOMPARE(r.peakDeg, 90.0); // −0.56 A at 90° el
    QCOMPARE(Seq::rippleFromCsv(wrapped, 0.15, 4).table, r.table); // −180 … 175° el: the same period
    // resampling: 10° steps given at 5°-offset angles interpolate linearly between them
    QByteArray offset;
    for (int d = 5; d < 360; d += 10) {
        offset += QByteArray::number(d) + "," + QByteArray::number(d % 20 == 5 ? 0.9 : -0.9) + "\n";
    }
    QCOMPARE(Seq::rippleFromCsv(offset, 0.15, 4).table, QVector<qint16>(Seq::RIPPLE_N, 0));
    QVERIFY(Seq::rippleFromCsv(QByteArray(csv).replace("\n45;\t", "\n#45;\t").replace("\n40 , ", "\n#40 , ").replace("\n50 , ", "\n#50 , "), 0.15, 4)
                .error.contains(QStringLiteral("no sample between 35° and 55°")));
    QVERIFY(Seq::rippleFromCsv(csv + "360,1\n", 0.15, 4).error.contains(QStringLiteral("two samples at 0°")));
    QVERIFY(Seq::rippleFromCsv(csv + "end\n", 0.15, 4).error.contains(QLatin1String("line 75")));
    QVERIFY(Seq::rippleFromCsv(QByteArray(csv).replace("90 , 1.500000", "90 , 30.000000"), 0.15, 4).error.contains(QStringLiteral("±30 A")));
    QVERIFY(!Seq::rippleFromCsv(csv, 0.0, 4).error.isEmpty());
    QVERIFY(Seq::rippleFromCsv("", 0.15, 4).error.contains(QLatin1String("no samples")));
}

QTEST_MAIN(TstReplay)
#include "tst_replay.moc"
