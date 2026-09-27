#include "IsoTp.h"

#include <algorithm>

namespace IsoTp {

int fdLen(int n)
{
    for (int l : {8, 12, 16, 20, 24, 32, 48}) {
        if (n <= l) {
            return l;
        }
    }
    return TX_DL;
}

QVector<QByteArray> frames(const QByteArray &msg)
{
    const int n = static_cast<int>(msg.size());
    const auto padded = [](QByteArray f) { return f.append(QByteArray(fdLen(static_cast<int>(f.size())) - f.size(), static_cast<char>(PAD))); };
    if (n == 0) {
        return {};
    }
    if (n <= 7) {
        return {padded(QByteArray(1, static_cast<char>(n)) + msg)};
    }
    if (n <= TX_DL - 2) {
        return {padded(QByteArray(1, '\0') + static_cast<char>(n) + msg)};
    }
    QByteArray ff;
    if (n <= 0xFFF) {
        ff += static_cast<char>(0x10 | (n >> 8));
        ff += static_cast<char>(n & 0xFF);
    } else {
        ff += static_cast<char>(0x10);
        ff += '\0';
        for (int s : {24, 16, 8, 0}) {
            ff += static_cast<char>((static_cast<quint32>(n) >> s) & 0xFF);
        }
    }
    int off = TX_DL - static_cast<int>(ff.size());
    QVector<QByteArray> out = {ff + msg.left(off)};
    for (int sn = 1; off < n; sn = (sn + 1) & 0x0F) {
        const int take = std::min(TX_DL - 1, n - off);
        out << padded(QByteArray(1, static_cast<char>(0x20 | sn)) + msg.mid(off, take));
        off += take;
    }
    return out;
}

int stminUs(quint8 x)
{
    if (x <= 0x7F) {
        return x * 1000;
    }
    return (x >= 0xF1 && x <= 0xF9) ? (x - 0xF0) * 100 : 127000;
}

} // namespace IsoTp

IsoTpSender::IsoTpSender(Out out, QObject *parent) : QObject(parent), m_out(std::move(out))
{
    m_nbs.setSingleShot(true);
    m_pace.setSingleShot(true);
    m_pace.setTimerType(Qt::PreciseTimer);
    connect(&m_nbs, &QTimer::timeout, this, [this] {
        cancel(QStringLiteral("no flow control within %1 ms (N_Bs) after frame %2 of %3").arg(IsoTp::N_BS_MS).arg(m_next).arg(m_frames.size()));
    });
    connect(&m_pace, &QTimer::timeout, this, &IsoTpSender::next);
}

bool IsoTpSender::start(const QVector<QByteArray> &frames, QString *err)
{
    if (active() || frames.size() < 2) {
        *err = active() ? QStringLiteral("a segmented UDS request is in flight") : QStringLiteral("not a segmented request");
        return false;
    }
    m_frames = frames;
    m_next = 1;
    m_fcs = m_waits = m_waitRow = 0;
    m_bs = m_stmin = -1;
    m_waitFc = true;
    if (!m_out(m_frames[0], true)) {
        m_frames.clear();
        *err = QStringLiteral("the first frame could not be written");
        return false;
    }
    m_nbs.start(IsoTp::N_BS_MS);
    return true;
}

bool IsoTpSender::flowControl(const QByteArray &f)
{
    if (!active() || !m_waitFc || f.size() < 3 || (static_cast<quint8>(f[0]) >> 4) != 3) {
        return false;
    }
    m_fcs++;
    const int fs = static_cast<quint8>(f[0]) & 0x0F;
    if (fs == 1) { // Wait
        m_waits++;
        if (++m_waitRow > IsoTp::N_WFT_MAX) {
            cancel(QStringLiteral("%1 Wait flow controls in a row (N_WFTmax %2)").arg(m_waitRow).arg(IsoTp::N_WFT_MAX));
        } else {
            m_nbs.start(IsoTp::N_BS_MS);
        }
        return true;
    }
    if (fs != 0) {
        cancel(fs == 2 ? QStringLiteral("flow control Overflow (32): the receiver cannot take a request this long")
                       : QStringLiteral("invalid flow status %1 in the flow control %2").arg(fs).arg(QString::fromLatin1(f.left(3).toHex(' ').toUpper())));
        return true;
    }
    m_nbs.stop();
    m_waitFc = false;
    m_waitRow = 0;
    m_bs = static_cast<quint8>(f[1]);
    m_stmin = static_cast<quint8>(f[2]);
    m_blockLeft = m_bs;
    next();
    return true;
}

void IsoTpSender::next()
{
    while (active() && !m_waitFc) {
        const bool last = m_next == m_frames.size() - 1;
        const bool blockEnd = !last && m_bs > 0 && m_blockLeft == 1;
        if (!m_out(m_frames[m_next], last || blockEnd)) {
            cancel(QStringLiteral("consecutive frame %1 of %2 could not be written").arg(m_next).arg(m_frames.size() - 1));
            return;
        }
        m_next++;
        if (last) {
            const QJsonObject i = info();
            abort();
            Q_EMIT sent(i);
            return;
        }
        if (blockEnd) {
            m_waitFc = true;
            m_nbs.start(IsoTp::N_BS_MS);
            return;
        }
        m_blockLeft -= (m_bs > 0) ? 1 : 0;
        const int us = IsoTp::stminUs(static_cast<quint8>(m_stmin));
        if (us > 0) {
            m_pace.start((us + 999) / 1000);
            return;
        }
    }
}

void IsoTpSender::cancel(const QString &why)
{
    if (active()) {
        const QJsonObject i = info();
        abort();
        Q_EMIT failed(why, i);
    }
}

void IsoTpSender::abort()
{
    m_nbs.stop();
    m_pace.stop();
    m_frames.clear();
    m_waitFc = false;
}

QJsonObject IsoTpSender::info() const
{
    return {{QStringLiteral("tx_frames"), m_next}, {QStringLiteral("tx_dl"), IsoTp::TX_DL}, {QStringLiteral("fc"), m_fcs},
            {QStringLiteral("fc_bs"), m_bs},       {QStringLiteral("fc_stmin"), m_stmin},  {QStringLiteral("fc_wait"), m_waits}};
}
