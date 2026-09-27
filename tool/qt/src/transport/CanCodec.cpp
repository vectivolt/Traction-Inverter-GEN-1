#include "CanCodec.h"

#include "Telemetry.h"

#include <QRegularExpression>

#include <algorithm>

namespace CanCodec {
namespace {
float clampf(float x, float lo, float hi) { return std::min(std::max(x, lo), hi); }
qint16 i16(const QByteArray &d, int at)
{
    return static_cast<qint16>(static_cast<quint16>(static_cast<quint8>(d[at]) | (static_cast<quint8>(d[at + 1]) << 8)));
}
quint16 u16(const QByteArray &d, int at)
{
    return static_cast<quint16>(static_cast<quint8>(d[at]) | (static_cast<quint8>(d[at + 1]) << 8));
}
void put16(QByteArray &d, int at, quint16 v)
{
    d[at] = static_cast<char>(v & 0xFFu);
    d[at + 1] = static_cast<char>(v >> 8);
}
quint8 byte(const QByteArray &d, int at) { return static_cast<quint8>(d[at]); }
// (uint16_t)(int16_t)ti_clampf(x, -32767, 32767): the conversion truncates toward zero
quint16 si16(float x) { return static_cast<quint16>(static_cast<qint16>(clampf(x, -32767.0f, 32767.0f))); }
void finish(QByteArray &f, quint32 id) { f[0] = static_cast<char>(e2eCrc(id, f)); }
} // namespace

quint8 crc8_1d(const quint8 *data, int len, quint8 init, quint8 xorout)
{
    quint8 crc = init;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80u) ? static_cast<quint8>(static_cast<quint8>(crc << 1) ^ 0x1Du) : static_cast<quint8>(crc << 1);
        }
    }
    return static_cast<quint8>(crc ^ xorout);
}

quint8 e2eCrc(quint32 id, const QByteArray &frame)
{
    QByteArray buf = frame;
    if (buf.isEmpty()) {
        return 0;
    }
    buf[0] = static_cast<char>(id & 0xFFu); // DataID
    return crc8_1d(reinterpret_cast<const quint8 *>(buf.constData()), static_cast<int>(buf.size()), 0xFF, 0xFF);
}

bool e2eOk(quint32 id, const QByteArray &frame) { return !frame.isEmpty() && e2eCrc(id, frame) == byte(frame, 0); }

QByteArray encodeVcuCmd(const VcuCmd &c)
{
    QByteArray f(LEN_VCU, '\0');
    f[1] = static_cast<char>((c.ctr & 0x0Fu) | ((static_cast<quint8>(c.gear) & 0x03u) << 4) | (c.enable ? 0x40u : 0u) |
                             (c.faultReset ? 0x80u : 0u));
    put16(f, 2, si16(c.torqueNm * 10.0f));
    const quint8 cs = (c.contactors == Contactors::Open) ? 1u
                    : (c.contactors == Contactors::Precharge) ? 2u
                    : (c.contactors == Contactors::Closed) ? 3u : 0u;
    f[4] = static_cast<char>(cs | (c.retryAuth ? 0x04u : 0u) | (c.discharge ? 0x08u : 0u) | (c.shutdown ? 0x10u : 0u) |
                             (c.vspeedValid ? 0x20u : 0u));
    f[5] = static_cast<char>(c.coolantValid ? static_cast<quint8>(clampf(c.coolantC + 40.0f, 0.0f, 254.0f)) : 0xFFu);
    put16(f, 6, static_cast<quint16>(clampf(c.vspeedKmh * 100.0f, 0.0f, 65535.0f))); // truncates like the C cast
    finish(f, ID_VCU_CMD);
    return f;
}

std::optional<VcuCmd> decodeVcuCmd(const QByteArray &f)
{
    if (f.size() < LEN_VCU || !e2eOk(ID_VCU_CMD, f.left(LEN_VCU))) {
        return std::nullopt;
    }
    VcuCmd c;
    const quint8 b1 = byte(f, 1);
    c.ctr = b1 & 0x0Fu;
    c.gear = static_cast<Gear>((b1 >> 4) & 0x03u);
    c.enable = (b1 & 0x40u) != 0;
    c.faultReset = (b1 & 0x80u) != 0;
    c.torqueNm = 0.1f * static_cast<float>(i16(f, 2));
    const quint8 cs = byte(f, 4) & 0x03u;
    c.contactors = static_cast<Contactors>(cs); // 1 open, 2 precharge, 3 closed, 0 invalid: the enum's own order
    c.retryAuth = (byte(f, 4) & 0x04u) != 0;
    c.discharge = (byte(f, 4) & 0x08u) != 0;
    c.shutdown = (byte(f, 4) & 0x10u) != 0;
    c.coolantValid = byte(f, 5) != 0xFFu;
    c.coolantC = static_cast<float>(byte(f, 5)) - 40.0f;
    c.vspeedValid = (byte(f, 4) & 0x20u) != 0;
    c.vspeedKmh = 0.01f * static_cast<float>(u16(f, 6));
    return c;
}

QByteArray encodeVcuBms(const VcuBms &b)
{
    QByteArray f(LEN_VCU, '\0');
    f[1] = static_cast<char>(b.ctr & 0x0Fu);
    put16(f, 2, static_cast<quint16>(clampf(b.packV * 10.0f, 0.0f, 65535.0f)));
    put16(f, 4, static_cast<quint16>(clampf(b.chgW / 100.0f, 0.0f, 65535.0f)));
    put16(f, 6, static_cast<quint16>(clampf(b.disW / 100.0f, 0.0f, 65535.0f)));
    finish(f, ID_VCU_BMS);
    return f;
}

std::optional<VcuBms> decodeVcuBms(const QByteArray &f)
{
    if (f.size() < LEN_VCU || !e2eOk(ID_VCU_BMS, f.left(LEN_VCU))) {
        return std::nullopt;
    }
    VcuBms b;
    b.ctr = byte(f, 1) & 0x0Fu;
    b.packV = 0.1f * static_cast<float>(u16(f, 2));
    b.chgW = 100.0f * static_cast<float>(u16(f, 4));
    b.disW = 100.0f * static_cast<float>(u16(f, 6));
    return b;
}

QByteArray encodeInvStatus(const InvStatus &s)
{
    QByteArray f(LEN_STATUS, '\0');
    f[1] = static_cast<char>((s.ctr & 0x0Fu) | (s.selfTestDone ? 0x10u : 0u) | (s.keepHv ? 0x20u : 0u) |
                             (s.derate ? 0x40u : 0u) | (s.fault ? 0x80u : 0u));
    f[2] = static_cast<char>(s.state);
    f[3] = static_cast<char>((s.bridge & 0x03u) | ((s.hv & 0x03u) << 2) | (s.zeroTorque ? 0x10u : 0u) |
                             (s.discharging ? 0x20u : 0u) | (s.prechargeRefused ? 0x40u : 0u) | (s.speedValid ? 0x80u : 0u));
    put16(f, 4, si16(s.torqueAppliedNm * 10.0f));
    put16(f, 6, si16(s.speedRpm));
    put16(f, 8, s.vdcValid ? static_cast<quint16>(clampf(s.vdcV * 10.0f, 0.0f, 65534.0f)) : 0xFFFFu);
    f[10] = static_cast<char>(static_cast<quint8>(clampf(s.tModuleC + 40.0f, 0.0f, 254.0f)));
    f[11] = static_cast<char>(s.nDtc);
    put16(f, 12, s.firstDtc);
    f[14] = static_cast<char>((s.noSafeState ? 0x01u : 0u) | (s.serviceRequired ? 0x02u : 0u) |
                              (s.openContactorsReq ? 0x04u : 0u) | (s.speedLimitReq ? 0x08u : 0u));
    f[15] = static_cast<char>(s.evidenceMissing & 0x1Fu);
    put16(f, 16, si16(s.torqueCmdNm * 10.0f));
    finish(f, ID_INV_STATUS);
    return f;
}

std::optional<InvStatus> decodeInvStatus(const QByteArray &f)
{
    if ((f.size() != LEN_STATUS && f.size() != LEN_STATUS_LEGACY) || !e2eOk(ID_INV_STATUS, f)) {
        return std::nullopt;
    }
    InvStatus s;
    const quint8 b1 = byte(f, 1);
    const quint8 b3 = byte(f, 3);
    const quint8 b14 = byte(f, 14);
    s.ctr = b1 & 0x0Fu;
    s.selfTestDone = (b1 & 0x10u) != 0;
    s.keepHv = (b1 & 0x20u) != 0;
    s.derate = (b1 & 0x40u) != 0;
    s.fault = (b1 & 0x80u) != 0;
    s.state = byte(f, 2);
    s.bridge = b3 & 0x03u;
    s.hv = (b3 >> 2) & 0x03u;
    s.zeroTorque = (b3 & 0x10u) != 0;
    s.discharging = (b3 & 0x20u) != 0;
    s.prechargeRefused = (b3 & 0x40u) != 0;
    s.speedValid = (b3 & 0x80u) != 0;
    s.torqueAppliedNm = 0.1f * static_cast<float>(i16(f, 4));
    s.speedRpm = static_cast<float>(i16(f, 6));
    const quint16 v = u16(f, 8);
    s.vdcValid = v != 0xFFFFu;
    s.vdcV = s.vdcValid ? 0.1f * static_cast<float>(v) : 0.0f;
    s.tModuleC = static_cast<float>(byte(f, 10)) - 40.0f;
    s.nDtc = byte(f, 11);
    s.firstDtc = u16(f, 12);
    s.noSafeState = (b14 & 0x01u) != 0;
    s.serviceRequired = (b14 & 0x02u) != 0;
    s.openContactorsReq = (b14 & 0x04u) != 0;
    s.speedLimitReq = (b14 & 0x08u) != 0;
    s.evidenceMissing = byte(f, 15) & 0x1Fu;
    s.hasTorqueCmd = f.size() >= LEN_STATUS;
    s.torqueCmdNm = s.hasTorqueCmd ? 0.1f * static_cast<float>(i16(f, 16)) : 0.0f;
    return s;
}

void addChannels(const InvStatus &s, TelemetryFrame &f)
{
    auto put = [&f](const char *k, double v) { f.num.insert(QLatin1String(k), v); };
    put("inv.ctr", s.ctr);
    put("inv.self_test_done", s.selfTestDone);
    put("inv.keep_hv", s.keepHv);
    put("inv.derate", s.derate);
    put("inv.fault", s.fault);
    put("inv.state", s.state);
    put("inv.bridge", s.bridge);
    put("inv.hv", s.hv);
    put("inv.zero_torque", s.zeroTorque);
    put("inv.discharging", s.discharging);
    put("inv.precharge_refused", s.prechargeRefused);
    put("inv.speed_valid", s.speedValid);
    put("inv.torque_applied_nm", s.torqueAppliedNm);
    put("inv.speed_rpm", s.speedRpm);
    put("inv.vdc_v", s.vdcValid ? s.vdcV : TelemetryFrame::nan());
    put("inv.vdc_valid", s.vdcValid);
    put("inv.t_module_c", s.tModuleC);
    put("inv.n_dtc", s.nDtc);
    put("inv.first_dtc", s.firstDtc);
    put("inv.no_safe_state", s.noSafeState);
    put("inv.service_required", s.serviceRequired);
    put("inv.open_contactors_req", s.openContactorsReq);
    put("inv.speed_limit_req", s.speedLimitReq);
    put("inv.evidence_missing", s.evidenceMissing);
    if (s.hasTorqueCmd) {
        put("inv.torque_cmd_nm", s.torqueCmdNm);
    }
}

void addChannelsFromHexText(TelemetryFrame &f)
{
    const QString hex = f.text.value(QStringLiteral("can"));
    if (!hex.isEmpty()) {
        if (const auto s = decodeInvStatus(fromHex(hex))) {
            addChannels(*s, f);
        }
    }
}

QByteArray fromHex(const QString &hex)
{
    QString h = hex;
    h.remove(QRegularExpression(QStringLiteral("[\\s:,]")));
    if (h.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        h = h.mid(2);
    }
    if ((h.size() % 2) != 0 || h.contains(QRegularExpression(QStringLiteral("[^0-9A-Fa-f]")))) {
        return {};
    }
    return QByteArray::fromHex(h.toLatin1());
}

AliveCounter::Result AliveCounter::check(quint8 ctr)
{
    ctr &= 0x0Fu;
    if (!m_have) {
        m_have = true;
        m_last = ctr;
        return Result::First;
    }
    const quint8 delta = static_cast<quint8>((ctr - m_last) & 0x0Fu);
    if (delta == 0) {
        return Result::Frozen;
    }
    m_last = ctr;
    return (delta > m_maxJump) ? Result::Jump : Result::Ok;
}

} // namespace CanCodec
