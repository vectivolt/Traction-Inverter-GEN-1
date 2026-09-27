#include "FirmwareUpdater.h"

#include "CanCodec.h"
#include "ProtocolInfo.h"
#include "Uds.h"

#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QtEndian>

#include <memory>
#include <utility>

namespace {
constexpr quint8 SID_DSC = 0x10, SID_ER = 0x11, SID_SA = 0x27, SID_RC = 0x31, SID_RD = 0x34, SID_TD = 0x36, SID_RTE = 0x37;
constexpr int MAX_BUSY = 100;        // NRC 0x21 repeats in one update before it gives up
constexpr int BUSY_WAIT_MS = 10;     // before a repeat: the background loop programs a chunk per pass
constexpr int VERIFY_POLL_MS = 20;
constexpr int VERIFY_POLLS = 500;    // 10 s: the background loop hashes the image read back from flash
constexpr int RESTART_POLL_MS = 250;
constexpr int RESTART_POLLS = 120;   // 30 s: a bootloader copies and checks up to a region before the image starts
constexpr int FD20_LEN = 17, F208_LEN = 16, FD24_LEN = 9; // uds_diag.c DIDS[]

QByteArray bytes(std::initializer_list<int> b)
{
    QByteArray a;
    for (int x : b) {
        a.append(static_cast<char>(x));
    }
    return a;
}
quint8 at(const QByteArray &a, int i) { return static_cast<quint8>(a.at(i)); }
int nrcOf(const QByteArray &r, quint8 sid) { return (r.size() >= 3 && at(r, 0) == 0x7F && at(r, 1) == sid) ? at(r, 2) : -1; }
QString hexOf(const QByteArray &b, int max = 12)
{
    return QString::fromLatin1(b.left(max).toHex(' ').toUpper()) + (b.size() > max ? QStringLiteral(" …") : QString());
}
QString hex32(quint32 v) { return QStringLiteral("0x%1").arg(v, 8, 16, QLatin1Char('0')).toUpper().replace(QLatin1String("0X"), QLatin1String("0x")); }
QString framing(int len) // how the transport frames a request of this length (IsoTp.h)
{
    return len <= 7 ? QStringLiteral("a classic single frame") : len <= 62 ? QStringLiteral("an escape single frame")
                                                                           : QStringLiteral("a first frame and %1 consecutive frames").arg(len / 63); // ceil((len - 62) / 63)
}
// firmware/src/boot/image.h img_result_t, boot.h boot_state_t / boot_last_t
const char *const IMG_RESULTS[][2] = {
    {"OK", "every check holds"},
    {"READ", "the staging region could not be read"},
    {"MAGIC", "not a TIFW container"},
    {"FORMAT", "format version or header length"},
    {"FIELD", "reserved bytes not zero, or a target outside 1..4"},
    {"LENGTH", "payload length 0 or beyond the region"},
    {"NO_KEY", "no release key provisioned on this card: nothing verifies"},
    {"SIGNATURE", "the Ed25519 signature does not verify under this card's release key"},
    {"TARGET", "signed for another SKU — or no boot record, so this card's target is unknown"},
    {"ROLLBACK", "security version below this card's anti-rollback counter"},
    {"HASH", "the payload is not the one signed"},
};
const char *const BOOT_STATES[] = {"IDLE", "ACTIVATE", "INSTALL", "TRIAL", "CONFIRMED", "COMMIT", "RESTORE"};
const char *const BOOT_LAST[] = {"NONE", "INSTALLED", "REJECTED", "NO_FALLBACK", "ABANDONED", "FIRST_BOOT_FAILED", "EXEC_INVALID"};
} // namespace

FirmwareUpdater::Image FirmwareUpdater::parse(const QByteArray &f)
{
    Image im;
    im.bytes = f;
    if (f.size() < HDR_LEN) {
        im.error = QStringLiteral("%1 bytes: shorter than the 128-byte header").arg(f.size());
        return im;
    }
    const auto *p = reinterpret_cast<const uchar *>(f.constData());
    const quint16 format = qFromLittleEndian<quint16>(p + 4), hdr = qFromLittleEndian<quint16>(p + 6);
    im.target = qFromLittleEndian<quint32>(p + 8);
    im.length = qFromLittleEndian<quint32>(p + 12);
    im.fwId = qFromLittleEndian<quint32>(p + 16);
    im.secVer = qFromLittleEndian<quint32>(p + 20);
    im.hashOk = QCryptographicHash::hash(f.mid(HDR_LEN), QCryptographicHash::Sha256) == f.mid(32, 32);
    im.signature = f.mid(64, 64) != QByteArray(64, '\0');
    if (!f.startsWith("TIFW")) {
        im.error = QStringLiteral("not a FW-38 container: the magic is not \"TIFW\"");
    } else if (format != 1 || hdr != HDR_LEN) {
        im.error = QStringLiteral("format %1 with a %2-byte header: this tool reads format 1, 128 bytes").arg(format).arg(hdr);
    } else if (f.mid(24, 8) != QByteArray(8, '\0')) {
        im.error = QStringLiteral("the reserved bytes 24..31 are not zero (the firmware refuses it: FIELD)");
    } else if (im.target < 1 || im.target > 4) {
        im.error = QStringLiteral("target %1: a SKU is 1..4 (the firmware refuses it: FIELD)").arg(im.target);
    } else if (im.length == 0 || static_cast<qint64>(im.length) + HDR_LEN != f.size()) {
        im.error = QStringLiteral("the header's payload length is %1 bytes, the file holds %2 after the header").arg(im.length).arg(f.size() - HDR_LEN);
    } else if (!im.hashOk) {
        im.error = QStringLiteral("the payload is not the one the header's SHA-256 names (the firmware refuses it: HASH)");
    } else if (!im.signature) {
        im.error = QStringLiteral("unsigned: the 64 signature bytes are zero");
    }
    return im;
}

FirmwareUpdater::Image FirmwareUpdater::load(const QString &path)
{
    QFile f(path);
    Image im;
    if (!f.open(QIODevice::ReadOnly)) {
        im.error = QStringLiteral("cannot read %1: %2").arg(path, f.errorString());
    } else {
        im = parse(f.readAll());
    }
    im.path = path;
    return im;
}

FirmwareUpdater::FirmwareUpdater(Session &s, QObject *parent) : QObject(parent), m_s(s)
{
    connect(&m_s, &Session::transportChanged, this, [this] {
        m_before = m_after = Identity();
        m_run++; // a read of the old transport ends with it
        m_reading = false;
        m_joined.clear();
        if (m_busy) { // no callback of the old transport continues it
            finish(false, QStringLiteral("the transport changed during the update"));
        }
        Q_EMIT changed();
    });
}

int FirmwareUpdater::blocksTotal() const
{
    const int data = m_blockMax - 2;
    return data > 0 ? static_cast<int>((m_img.bytes.size() + data - 1) / data) : 0;
}

void FirmwareUpdater::request(const QByteArray &msg, Reply next, int timeoutMs)
{
    QPointer<FirmwareUpdater> self(this);
    const int run = m_run;
    m_s.command(QStringLiteral("uds"), {{QStringLiteral("msg"), QString::fromLatin1(msg.toHex(' ').toUpper())}},
                [self, run, next](const Ack &a) {
                    if (!self || run != self->m_run) {
                        return; // an update ended (or began) since: not this sequence's any more
                    }
                    const QByteArray r = CanCodec::fromHex(a.raw.value(QLatin1String("msg")).toString());
                    if (a.raw.contains(QLatin1String("tx_frames"))) { // a segmented request: how it went out
                        self->m_tx = a.raw;
                    }
                    next(r, !a.ok ? (a.message.isEmpty() ? QStringLiteral("refused") : a.message)
                                  : r.isEmpty() ? QStringLiteral("no response (%1)").arg(a.message.isEmpty() ? QStringLiteral("nothing came") : a.message)
                                                : QString());
                },
                timeoutMs);
}

void FirmwareUpdater::later(int ms, std::function<void()> f)
{
    const int run = m_run;
    QTimer::singleShot(ms, this, [this, run, f = std::move(f)] {
        if (run == m_run) {
            f();
        }
    });
}

bool FirmwareUpdater::expect(const QByteArray &r, const QString &err, const QByteArray &want, const QString &what)
{
    if (err.isEmpty() && r.startsWith(want)) {
        return true;
    }
    const auto sid = static_cast<quint8>(static_cast<quint8>(want[0]) - 0x40);
    m_nrc = nrcOf(r, sid);
    finish(false, QStringLiteral("%1: %2").arg(what, !err.isEmpty() ? err
                                                     : m_nrc >= 0   ? nrcText(sid, m_nrc)
                                                                    : QStringLiteral("unexpected response %1").arg(hexOf(r))));
    return false;
}

bool FirmwareUpdater::busyRepeat(const QByteArray &r, const QString &err, quint8 sid, const QString &what, std::function<void()> again)
{
    if (!err.isEmpty() || nrcOf(r, sid) != 0x21) {
        return false;
    }
    if (++m_busyRepeats > MAX_BUSY) {
        m_nrc = 0x21;
        finish(false, QStringLiteral("%1: NRC 0x21 busyRepeatRequest %2 times in this update — given up").arg(what).arg(m_busyRepeats - 1));
        return true;
    }
    m_s.addEvent(QStringLiteral("info"), QStringLiteral("firmware update: %1 — NRC 0x21 busyRepeatRequest, the ECU asks for it again "
                                                        "(repeat %2)").arg(what).arg(m_busyRepeats));
    later(BUSY_WAIT_MS, std::move(again));
    Q_EMIT changed();
    return true;
}

void FirmwareUpdater::setStep(Step s)
{
    m_step = s;
    Q_EMIT changed();
}

// ---------------- the running image ----------------
bool FirmwareUpdater::readIdentity(std::function<void()> done)
{
    if (m_busy || !m_s.isDeviceLive() || !m_s.transport()->commands().contains(QStringLiteral("uds"))) {
        return false;
    }
    if (done) {
        m_joined << std::move(done);
    }
    if (m_reading) {
        return true;
    }
    m_reading = true;
    readInto(&m_before, [this] {
        m_reading = false;
        m_after = Identity(); // the last update's "after" is history now
        const auto joined = std::exchange(m_joined, {});
        Q_EMIT changed();
        for (const auto &f : joined) {
            f();
        }
    });
    return true;
}

// The simulator's hello first (its power-up counter), then DID 0xFD20 (TI_FW_ID, the three SKUs, the serial), 0xF208
// (uptime, key cycle) and 0xFD24 (the FW-38 boot record: the anti-rollback counter) in one ReadDataByIdentifier — the
// boot record from the DID on both transports.
void FirmwareUpdater::readInto(Identity *out, std::function<void()> done)
{
    auto n = std::make_shared<Identity>();
    auto dids = [this, out, done, n] {
        request(bytes({0x22, 0xFD, 0x20, 0xF2, 0x08, 0xFD, 0x24}), [out, done, n](const QByteArray &r, const QString &err) {
            if (!err.isEmpty() || r.isEmpty() || at(r, 0) != 0x62) {
                n->error = !err.isEmpty() ? err : QStringLiteral("DID 0xFD20: %1").arg(hexOf(r));
            }
            for (int i = 1; n->error.isEmpty() && i + 2 <= r.size();) {
                const int did = (at(r, i) << 8) | at(r, i + 1);
                const int len = did == 0xFD20 ? FD20_LEN : did == 0xF208 ? F208_LEN : did == 0xFD24 ? FD24_LEN : -1;
                if (len < 0 || i + 2 + len > r.size()) {
                    break;
                }
                const auto *d = reinterpret_cast<const uchar *>(r.constData()) + i + 2;
                if (did == 0xFD20) { // did_identity: TI_FW_ID, p->sku, hw_sku, cal.sku, serial[8], DTC_COUNT - 1
                    n->valid = true;
                    n->fwId = qFromBigEndian<quint32>(d);
                    n->sku = d[4];
                    n->hwSku = d[5];
                    n->calSku = d[6];
                    n->serial = QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(d + 7), 8)).remove(QChar(0));
                } else if (did == 0xF208) { // did_uptime: now_ms, key_cycle, cold start, ...
                    n->haveUptime = true;
                    n->uptimeMs = qFromBigEndian<quint32>(d);
                    n->keyCycle = qFromBigEndian<quint32>(d + 4);
                } else if (d[0] != 0xFF) { // did_boot: state (0xFF: no record), last, target, counter BE32, lkg_valid, last_err
                    n->bootRec = QJsonObject{{QStringLiteral("state"), d[0]},
                                             {QStringLiteral("last"), d[1]},
                                             {QStringLiteral("target"), d[2]},
                                             {QStringLiteral("sec_counter"), static_cast<qint64>(qFromBigEndian<quint32>(d + 3))},
                                             {QStringLiteral("lkg_valid"), d[7] != 0},
                                             {QStringLiteral("last_err"), d[8]}};
                }
                i += 2 + len;
            }
            *out = *n;
            done();
        });
    };
    if (!m_s.transport()->isSimulator()) {
        dids();
        return;
    }
    QPointer<FirmwareUpdater> self(this);
    const int run = m_run;
    m_s.command(QStringLiteral("info"), {}, [self, run, n, dids](const Ack &) {
        if (!self || run != self->m_run) {
            return;
        }
        n->boot = self->m_s.deviceInfo().value(QLatin1String("boot")).toInt(-1); // the hello precedes the ack (PROTOCOL.md A.4.1)
        dids();
    });
}

QStringList FirmwareUpdater::warnings(const Image &img) const
{
    QStringList w;
    if (!m_before.valid) {
        w << QStringLiteral("the running image is unknown (DID 0xFD20 not read%1): nothing could be compared")
                 .arg(m_before.error.isEmpty() ? QString() : QStringLiteral(": ") + m_before.error);
    } else {
        if (static_cast<int>(img.target) != m_before.sku) {
            w << QStringLiteral("signed for %1, but this card runs the %2 parameter set (HW_ID %3): the firmware refuses another "
                                "target (TARGET)").arg(skuName(static_cast<int>(img.target)), skuName(m_before.sku), skuName(m_before.hwSku));
        }
        if (img.fwId == m_before.fwId) {
            w << QStringLiteral("the running image's own TI_FW_ID %1").arg(hex32(img.fwId));
        } else if (img.fwId < m_before.fwId) {
            w << QStringLiteral("TI_FW_ID %1 is older than the running image's %2").arg(hex32(img.fwId), hex32(m_before.fwId));
        }
    }
    if (m_before.bootRec.contains(QLatin1String("sec_counter"))) {
        const auto counter = static_cast<quint32>(m_before.bootRec.value(QLatin1String("sec_counter")).toInteger());
        if (img.secVer < counter) {
            w << QStringLiteral("security version %1 is below this card's anti-rollback counter %2: the firmware refuses it "
                                "(ROLLBACK)").arg(img.secVer).arg(counter);
        }
        if (static_cast<int>(img.target) != m_before.bootRec.value(QLatin1String("target")).toInt()) {
            w << QStringLiteral("the card's boot record names %1 as its target").arg(skuName(m_before.bootRec.value(QLatin1String("target")).toInt()));
        }
    }
    return w;
}

// ---------------- the sequence ----------------
bool FirmwareUpdater::start(const Image &img)
{
    ITransport *t = m_s.transport();
    const QString why = m_busy                                                   ? QStringLiteral("an update is in progress")
                      : m_reading                                                ? QStringLiteral("the running image is being read")
                      : !img.ok()                                                ? QStringLiteral("the image: %1").arg(img.error)
                      : !m_s.isDeviceLive()                                      ? QStringLiteral("the link is not live")
                      : !t->commands().contains(QStringLiteral("uds"))           ? QStringLiteral("this transport has no UDS")
                      : (t->kind() == ITransport::Kind::Can && !m_s.serviceUnlocked()) ? QStringLiteral("transmitting on a CAN bus needs the service key")
                                                                                 : QString();
    if (!why.isEmpty()) {
        m_text = QStringLiteral("not started — %1").arg(why);
        Q_EMIT changed();
        return false;
    }
    m_run++;
    m_img = img;
    m_busy = true;
    m_activated = m_restarted = false;
    m_nrc = m_verify = m_reason = -1;
    m_blockMax = m_block = m_busyRepeats = 0;
    m_bytes = 0;
    m_elapsed = m_transferMs = -1;
    m_tx = QJsonObject();
    m_after = Identity();
    m_text.clear();
    m_clock.start();
    m_s.addEvent(QStringLiteral("warn"), QStringLiteral("firmware update started: %1 — %2, TI_FW_ID %3, security version %4, %5 bytes")
                                             .arg(img.path, skuName(static_cast<int>(img.target)), hex32(img.fwId)).arg(img.secVer).arg(img.bytes.size()));
    if (t->kind() != ITransport::Kind::Can) {
        unlock();
        return true;
    }
    setStep(Step::Check); // over CAN: the counter read now (DID 0xFD24), a rollback refused before anything is written
    readInto(&m_before, [this] {
        const QJsonObject &rec = m_before.bootRec;
        if (rec.contains(QLatin1String("sec_counter")) && m_img.secVer < rec.value(QLatin1String("sec_counter")).toInteger()) {
            finish(false, QStringLiteral("not started — security version %1 is below this card's anti-rollback counter %2 (DID 0xFD24): "
                                         "the firmware would refuse it at verification (ROLLBACK). Nothing was sent but the read.")
                              .arg(m_img.secVer).arg(rec.value(QLatin1String("sec_counter")).toInteger()));
            return;
        }
        unlock();
    });
    return true;
}

void FirmwareUpdater::unlock()
{
    setStep(Step::Unlock);
    request(bytes({SID_SA, 0x01}), [this](const QByteArray &r, const QString &err) {
        if (!expect(r, err, bytes({0x67, 0x01}), QStringLiteral("SecurityAccess requestSeed"))) {
            return;
        }
        const QByteArray seed = r.mid(2, 4);
        if (seed == QByteArray(4, '\0')) { // a zero seed: already unlocked
            session();
            return;
        }
        request(bytes({SID_SA, 0x02}) + Uds::benchKey(seed), [this](const QByteArray &k, const QString &kerr) {
            if (expect(k, kerr, bytes({0x67, 0x02}), QStringLiteral("SecurityAccess sendKey"))) {
                session();
            }
        });
    });
}

void FirmwareUpdater::session()
{
    setStep(Step::Session);
    request(bytes({SID_DSC, 0x02}), [this](const QByteArray &r, const QString &err) {
        if (expect(r, err, bytes({0x50, 0x02}), QStringLiteral("DiagnosticSessionControl programmingSession"))) {
            download();
        }
    });
}

void FirmwareUpdater::download()
{
    setStep(Step::Download);
    const auto size = static_cast<quint32>(m_img.bytes.size());
    const int n = size > 0xFFFFFF ? 4 : size > 0xFFFF ? 3 : size > 0xFF ? 2 : 1;
    // dataFormatIdentifier 0 (plain); addressAndLengthFormatIdentifier: the size's length (high nibble), the address's
    // (low): address 0 in one byte — the staging region — and the size in as few bytes as hold it
    QByteArray rq = bytes({SID_RD, 0x00, (n << 4) | 0x01, 0x00});
    for (int k = n - 1; k >= 0; k--) {
        rq += static_cast<char>((size >> (8 * k)) & 0xFF);
    }
    request(rq, [this](const QByteArray &r, const QString &err) {
        if (!expect(r, err, bytes({0x74}), QStringLiteral("RequestDownload"))) {
            return;
        }
        const int lf = r.size() >= 2 ? at(r, 1) >> 4 : 0;
        quint32 max = 0;
        for (int k = 0; k < lf && 2 + k < r.size(); k++) {
            max = (max << 8) | at(r, 2 + k);
        }
        if (lf < 1 || lf > 4 || r.size() < 2 + lf || max < 3 || max > 0xFFFF) {
            finish(false, QStringLiteral("RequestDownload: no usable maxNumberOfBlockLength in %1").arg(hexOf(r)));
            return;
        }
        m_blockMax = static_cast<int>(max);
        m_transferClock.start();
        setStep(Step::Transfer);
        transfer();
    });
}

void FirmwareUpdater::transfer()
{
    const int data = m_blockMax - 2;
    const qint64 off = static_cast<qint64>(m_block) * data;
    if (off >= m_img.bytes.size()) {
        m_transferMs = m_transferClock.elapsed();
        transferExit();
        return;
    }
    const QByteArray chunk = m_img.bytes.mid(off, data);
    const auto bsc = static_cast<quint8>((m_block + 1) & 0xFF);
    const QString what = QStringLiteral("TransferData block %1 of %2 (%3 bytes, %4)")
                             .arg(m_block + 1).arg(blocksTotal()).arg(chunk.size() + 2).arg(framing(static_cast<int>(chunk.size()) + 2));
    request(bytes({SID_TD, bsc}) + chunk, [this, bsc, what, n = chunk.size()](const QByteArray &r, const QString &err) {
        if (busyRepeat(r, err, SID_TD, what, [this] { transfer(); }) || !expect(r, err, bytes({0x76, bsc}), what)) {
            return;
        }
        m_bytes += n;
        m_block++;
        Q_EMIT changed();
        transfer();
    });
}

void FirmwareUpdater::transferExit()
{
    setStep(Step::Exit);
    request(bytes({SID_RTE}), [this](const QByteArray &r, const QString &err) {
        if (!expect(r, err, bytes({0x77}), QStringLiteral("RequestTransferExit"))) {
            return;
        }
        setStep(Step::Verify);
        request(bytes({SID_RC, 0x01, 0xFF, 0x01}), [this](const QByteArray &v, const QString &verr) {
            if (expect(v, verr, bytes({0x71, 0x01, 0xFF, 0x01}), QStringLiteral("RoutineControl start 0xFF01 (verify)"))) {
                verifyPoll(0);
            }
        });
    });
}

void FirmwareUpdater::verifyPoll(int polls)
{
    request(bytes({SID_RC, 0x03, 0xFF, 0x01}), [this, polls](const QByteArray &r, const QString &err) {
        if (!expect(r, err, bytes({0x71, 0x03, 0xFF, 0x01}), QStringLiteral("RoutineControl results 0xFF01 (verify)"))) {
            return;
        }
        if (r.size() < 6) {
            finish(false, QStringLiteral("RoutineControl results 0xFF01: %1 carries no status").arg(hexOf(r)));
            return;
        }
        m_verify = at(r, 4);
        m_reason = at(r, 5);
        Q_EMIT changed();
        if (m_verify == 0 && polls < VERIFY_POLLS) {
            later(VERIFY_POLL_MS, [this, polls] { verifyPoll(polls + 1); });
        } else if (m_verify == 1) {
            readInto(&m_before, [this] { activate(); }); // what the restart is judged against (the simulator: the boot record IDLE)
        } else {
            finish(false, m_verify == 2 ? QStringLiteral("the firmware refused the staged image: %1 — nothing is activated").arg(imgResultText(m_reason))
                                        : QStringLiteral("the verification did not end within %1 s").arg(VERIFY_POLLS * VERIFY_POLL_MS / 1000));
        }
    });
}

void FirmwareUpdater::activate()
{
    setStep(Step::Activate);
    request(bytes({SID_RC, 0x01, 0xF0, 0x38}), [this](const QByteArray &r, const QString &err) {
        if (!expect(r, err, bytes({0x71, 0x01, 0xF0, 0x38}), QStringLiteral("RoutineControl start 0xF038 (activate)"))) {
            return;
        }
        m_activated = true;
        m_s.addEvent(QStringLiteral("warn"), QStringLiteral("firmware update: activated — the boot record says ACTIVATE; the bootloader "
                                                            "checks the image again at the next reset"));
        reset(); // accepted once the record reads back from NVM (until then NRC 0x21)
    });
}

void FirmwareUpdater::reset()
{
    setStep(Step::Reset);
    request(bytes({SID_ER, 0x01}), [this](const QByteArray &r, const QString &err) {
        if (!busyRepeat(r, err, SID_ER, QStringLiteral("ECUReset"), [this] { reset(); }) &&
            expect(r, err, bytes({0x51, 0x01}), QStringLiteral("ECUReset hardReset"))) {
            setStep(Step::Restart);
            restart(0);
        }
    });
}

// The identity again every 250 ms until the ECU has restarted — a new key cycle, or the uptime back below what it was
// before the reset (the simulator: also its power-up counter).
void FirmwareUpdater::restart(int polls)
{
    later(RESTART_POLL_MS, [this, polls] {
        readInto(&m_after, [this, polls] {
            const Identity &b = m_before, &a = m_after;
            m_restarted = a.valid && a.haveUptime && b.haveUptime && (a.keyCycle != b.keyCycle || a.uptimeMs < b.uptimeMs || (a.boot >= 0 && a.boot != b.boot));
            if (!m_restarted && polls + 1 < RESTART_POLLS) {
                Q_EMIT changed();
                restart(polls + 1);
                return;
            }
            if (!m_restarted) {
                finish(false, QStringLiteral("activated and reset (51 01), but no restart seen within %1 s: key cycle %2, uptime %3 ms%4")
                                  .arg(RESTART_POLLS * RESTART_POLL_MS / 1000).arg(a.keyCycle).arg(a.uptimeMs)
                                  .arg(a.error.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(a.error)));
                return;
            }
            QString t = QStringLiteral("the ECU restarted: key cycle %1 → %2, uptime %3 ms → %4 ms").arg(b.keyCycle).arg(a.keyCycle).arg(b.uptimeMs).arg(a.uptimeMs);
            if (a.boot >= 0) {
                t += QStringLiteral(", power-up %1 → %2").arg(b.boot).arg(a.boot);
            }
            if (a.fwId == m_img.fwId && m_img.fwId != b.fwId) {
                t += QStringLiteral(". The new image runs (TI_FW_ID %1): its first boot is a trial — it confirms itself after 5 s of running, "
                                    "the next reset commits it and raises the anti-rollback counter to %2").arg(hex32(a.fwId)).arg(m_img.secVer);
            } else {
                t += QStringLiteral(". TI_FW_ID %1 runs (the image's: %2)").arg(hex32(a.fwId), hex32(m_img.fwId));
            }
            const QJsonObject &rec = a.bootRec;
            if (!rec.isEmpty()) {
                const int st = rec.value(QLatin1String("state")).toInt(-1);
                t += QStringLiteral(". Boot record (DID 0xFD24): %1, last %2, counter %3").arg(bootStateName(st), bootLastName(rec.value(QLatin1String("last")).toInt()))
                         .arg(rec.value(QLatin1String("sec_counter")).toInteger());
                if (st == 1 && a.boot >= 0) {
                    t += QStringLiteral(" — the host build has no bootloader: the staged image was verified and activated, never installed or run");
                }
            }
            finish(true, t);
        });
    });
}

void FirmwareUpdater::finish(bool ok, const QString &text)
{
    if (!m_busy) {
        return;
    }
    m_run++; // whatever is still in flight belongs to a sequence that has ended
    m_busy = false;
    m_step = ok ? Step::Done : Step::Failed;
    m_elapsed = m_clock.elapsed();
    m_text = text;
    m_s.addEvent(ok ? QStringLiteral("info") : QStringLiteral("warn"), QStringLiteral("firmware update: %1").arg(text));
    Q_EMIT changed();
}

// ---------------- names ----------------
QString FirmwareUpdater::stepName(Step s)
{
    static const char *const N[] = {"idle", "pre-check (DID 0xFD24)", "SecurityAccess", "programming session", "RequestDownload", "TransferData",
                                    "RequestTransferExit", "verify (0xFF01)", "activate (0xF038)", "ECUReset",
                                    "waiting for the restart", "done", "failed"};
    return QLatin1String(N[static_cast<int>(s)]);
}

QString FirmwareUpdater::skuName(int sku)
{
    const SkuInfo *i = ProtocolInfo::instance().sku(sku);
    return i ? QStringLiteral("%1 (%2)").arg(i->name).arg(sku) : QStringLiteral("SKU %1").arg(sku);
}

QString FirmwareUpdater::imgResultText(int r)
{
    return (r >= 0 && r < static_cast<int>(std::size(IMG_RESULTS))) ? QStringLiteral("%1: %2").arg(QLatin1String(IMG_RESULTS[r][0]), QString::fromUtf8(IMG_RESULTS[r][1]))
                                                                     : QStringLiteral("reason %1").arg(r);
}

QString FirmwareUpdater::bootStateName(int st)
{
    return (st >= 0 && st < static_cast<int>(std::size(BOOT_STATES))) ? QLatin1String(BOOT_STATES[st]) : QStringLiteral("state %1").arg(st);
}

QString FirmwareUpdater::bootLastName(int last)
{
    return (last >= 0 && last < static_cast<int>(std::size(BOOT_LAST))) ? QLatin1String(BOOT_LAST[last]) : QStringLiteral("%1").arg(last);
}

// ISO 14229-1's name, and what this firmware means by it at this service (uds.c, uds_update.c, update.c)
QString FirmwareUpdater::nrcText(quint8 sid, int nrc)
{
    static const QHash<int, const char *> names = {
        {0x10, "generalReject"}, {0x11, "serviceNotSupported"}, {0x12, "subFunctionNotSupported"},
        {0x13, "incorrectMessageLengthOrInvalidFormat"}, {0x21, "busyRepeatRequest"}, {0x22, "conditionsNotCorrect"},
        {0x24, "requestSequenceError"}, {0x31, "requestOutOfRange"}, {0x33, "securityAccessDenied"}, {0x35, "invalidKey"},
        {0x36, "exceededNumberOfAttempts"}, {0x70, "uploadDownloadNotAccepted"}, {0x71, "transferDataSuspended"},
        {0x72, "generalProgrammingFailure"}, {0x73, "wrongBlockSequenceCounter"}, {0x78, "responsePending"},
        {0x7E, "subFunctionNotSupportedInActiveSession"}, {0x7F, "serviceNotSupportedInActiveSession"}};
    static const QHash<int, const char *> here = {
        {0x1022, "the programming session needs the bridge disarmed, HV absent (the link discharged) and the motor at standstill"},
        {0x2722, "no SecurityAccess key in this firmware build (fail closed)"},
        {0x2735, "this tool sends the bench key (PROTOCOL.md A.4.1); a product build's key is the OEM's"},
        {0x2736, "three invalid keys lock SecurityAccess until the MCU restarts"},
        {0x347F, "not in the programming session"}, {0x367F, "not in the programming session"}, {0x377F, "not in the programming session"},
        {0x3433, "SecurityAccess is not unlocked"},
        {0x3422, "a download, a verification or an activation is already in progress"},
        {0x3421, "the background still owes flash work"},
        {0x3431, "address 0 (the staging region), a size above the header and within the region, no compression or encryption"},
        {0x3624, "no download in progress"}, {0x3672, "programming the staging flash failed"},
        {0x3673, "the block sequence counter is not the one due"}, {0x3671, "more data than RequestDownload announced"},
        {0x3613, "a block of 0 bytes or beyond the 4096-byte chunk"},
        {0x3724, "not every announced byte arrived"},
        {0x3131, "the routine exists only in the programming session"},
        {0x3124, "out of order: verify after RequestTransferExit, its results after a start, activate after a passed verification"},
        {0x3122, "the conditions again (the bridge disarmed, HV absent, standstill), and the boot record IDLE — no activation or trial pending"},
        {0x3172, "the boot record could not be queued to NVM"},
        {0x1121, "the background still owes flash or NVM work"},
        {0x1172, "the activation's boot record did not read back from NVM"}};
    const char *w = here.value((sid << 8) | nrc, nullptr);
    return QStringLiteral("NRC 0x%1 %2%3").arg(QString::number(nrc, 16).toUpper().rightJustified(2, QLatin1Char('0')),
                                              QLatin1String(names.value(nrc, "")), w ? QStringLiteral(" — %1").arg(QString::fromUtf8(w)) : QString());
}
