// CanCodec — the vehicle CAN-FD frames of firmware/src/comms/can_cmd.h, bit for bit (docs/firmware-contract.md
// FW-11, FW-08b, §10a, §10e). E2E: CRC-8 SAE J1850 (poly 0x1D, init 0xFF, xorout 0xFF) over the DataID (low byte of
// the CAN id) followed by payload bytes 1..len-1; byte 0 carries the CRC; a 4-bit alive counter in b1[3:0].
// Scalings convert through float exactly as the firmware does (a float-to-int conversion truncates toward zero).
//   VCU_CMD    0x101  8 B   VCU_BMS 0x102  8 B   INV_STATUS 0x201 20 B (16 B before round 23)
//   UDS        0x7E1 request / 0x7E9 response (ISO 15765-2) / 0x6E9 periodic frames on the diagnostic bus (FW-32, FW-40)
#pragma once

#include <QByteArray>
#include <QtGlobal>

#include <optional>

struct TelemetryFrame;

namespace CanCodec {

constexpr quint32 ID_VCU_CMD = 0x101;
constexpr quint32 ID_VCU_BMS = 0x102;
constexpr quint32 ID_INV_STATUS = 0x201;
constexpr quint32 ID_UDS_REQ = 0x7E1;
constexpr quint32 ID_UDS_RSP = 0x7E9;
constexpr quint32 ID_UDS_PERIODIC = 0x6E9;
constexpr int LEN_VCU = 8;
constexpr int LEN_STATUS = 20;
constexpr int LEN_STATUS_LEGACY = 16;
constexpr int STATUS_PERIOD_MS = 10; // app.c STATUS_PERIOD_MS

enum class Gear : quint8 { N = 0, D, R, P };
enum class Contactors : quint8 { Invalid = 0, Open, Precharge, Closed };

quint8 crc8_1d(const quint8 *data, int len, quint8 init, quint8 xorout);
quint8 e2eCrc(quint32 id, const QByteArray &frame); // over DataID + frame[1..size-1]
bool e2eOk(quint32 id, const QByteArray &frame);

struct VcuCmd {
    quint8 ctr = 0;
    Gear gear = Gear::N;
    bool enable = false;
    bool faultReset = false;
    float torqueNm = 0.0f;
    Contactors contactors = Contactors::Open;
    bool retryAuth = false;
    bool discharge = false;
    bool shutdown = false;
    float coolantC = 0.0f;
    bool coolantValid = true; // false: 0xFF "n/a" (the firmware's own encoder never sends it)
    bool vspeedValid = false; // round 23 (FW-39): b4 [5] — the vehicle speed below is valid
    float vspeedKmh = 0.0f;   // b6-7, 0.01 km/h, unsigned (the service mode needs valid AND zero)
};

struct VcuBms {
    quint8 ctr = 0;
    float packV = 0.0f;
    float chgW = 0.0f; // charge (regen) power limit
    float disW = 0.0f; // discharge power limit
};

struct InvStatus {
    quint8 ctr = 0;
    bool selfTestDone = false, keepHv = false, derate = false, fault = false;
    quint8 state = 0;
    quint8 bridge = 0; // 0 SPO, 1 idle (EN high), 2 modulating, 3 PWM-ASC
    quint8 hv = 0;     // 0 unknown, 1 safe, 2 present
    bool zeroTorque = false, discharging = false, prechargeRefused = false, speedValid = false;
    float torqueAppliedNm = 0.0f; // b4-5: the torque the issued current references represent (round 23)
    float speedRpm = 0.0f;
    float vdcV = 0.0f;
    bool vdcValid = false;
    float tModuleC = 0.0f;
    quint8 nDtc = 0;
    quint16 firstDtc = 0;
    bool noSafeState = false, serviceRequired = false, openContactorsReq = false, speedLimitReq = false;
    quint8 evidenceMissing = 0; // ARM_EV_* bits not present
    float torqueCmdNm = 0.0f;   // b16-17 (round 23)
    bool hasTorqueCmd = true;   // false for a legacy 16-byte frame
};

QByteArray encodeVcuCmd(const VcuCmd &c);
std::optional<VcuCmd> decodeVcuCmd(const QByteArray &f); // nullopt: short or CRC
QByteArray encodeVcuBms(const VcuBms &b);
std::optional<VcuBms> decodeVcuBms(const QByteArray &f);
QByteArray encodeInvStatus(const InvStatus &s); // 20 bytes (the mock inverter and the tests)
std::optional<InvStatus> decodeInvStatus(const QByteArray &f); // 20 B, or 16 B legacy; CRC checked
void addChannels(const InvStatus &s, TelemetryFrame &f);      // "inv.*"
// The INV_STATUS bytes a simulator frame carries as hex text "can" (when present and valid) -> "inv.*".
void addChannelsFromHexText(TelemetryFrame &f);
QByteArray fromHex(const QString &hex);                        // "0A 1B", "0a1b", "0A:1B"

// The firmware's alive-counter rule (can_cmd.c counter_ok): the first frame is accepted; a repeated counter is a
// frozen sender; a step above maxJump is rejected and resyncs.
class AliveCounter
{
public:
    enum class Result { First, Ok, Frozen, Jump };
    explicit AliveCounter(int maxJump = 2) : m_maxJump(maxJump) {}
    Result check(quint8 ctr);
    void reset() { m_have = false; }
    void setMaxJump(int j) { m_maxJump = j; }

private:
    int m_maxJump;
    bool m_have = false;
    quint8 m_last = 0;
};

} // namespace CanCodec
