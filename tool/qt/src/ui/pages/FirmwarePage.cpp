#include "FirmwarePage.h"

#include "ControlsPage.h"
#include "ProtocolInfo.h"
#include "Widgets.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTimer>

#include <algorithm>

namespace {
QString hex32(double v) { return QStringLiteral("0x%1").arg(static_cast<qulonglong>(v), 8, 16, QLatin1Char('0')).toUpper().replace(QLatin1String("0X"), QLatin1String("0x")); }
QString yesNo(const QJsonValue &v) { return v.isUndefined() ? QStringLiteral("—") : (v.toBool() ? QStringLiteral("yes") : QStringLiteral("no")); }
QString fwHex(quint32 v) { return hex32(static_cast<double>(v)); }
} // namespace

FirmwarePage::FirmwarePage(Session &s, QWidget *parent)
    : Page(s, QStringLiteral("Firmware"),
           QStringLiteral("The running image and its records, and the FW-38 field update: a signed image the firmware verifies "
                          "before it activates it. A new TI_FW_ID needs its own EOL/HIL validation record before it arms (FW-24); "
                          "the calibration record is bound to the card, the SKU and a motor (FW-20)."),
           parent),
      m_up(s)
{
    addRootBanner();
    QVBoxLayout *root = scrollContent();
    m_canNote = new QLabel(QStringLiteral("Connected over CAN: INV_STATUS carries the arming evidence only. The records below come "
                                          "from the simulator bridge; on a real bench they are EOL/UDS items. The update reads "
                                          "TI_FW_ID (DID 0xFD20) and the anti-rollback counter (DID 0xFD24) over UDS and "
                                          "refuses an image below the counter before anything is sent; the root of trust "
                                          "is DID 0xFD23."));
    m_canNote->setObjectName(QStringLiteral("pill"));
    m_canNote->setProperty("severity", QStringLiteral("info"));
    m_canNote->setWordWrap(true);
    m_canNote->hide();
    root->addWidget(m_canNote);
    auto *grid = new QGridLayout;
    grid->setSpacing(Theme::S3);

    auto *idCard = new Card(QStringLiteral("Image identity"));
    m_fwPill = makePill(QStringLiteral("UNKNOWN"), Theme::Sev::Stale);
    idCard->addHeaderWidget(m_fwPill);
    m_ident = new KvGrid;
    for (const auto &r : {std::pair<const char *, const char *>{"fw", "TI_FW_ID (device)"}, {"exp", "TI_FW_ID (this build's exports)"},
                          {"sku", "SKU"}, {"bridge", "Bridge"}, {"boot", "Power-up / key cycle"}, {"fsw", "f_sw / loop / crossover"},
                          {"nx", "n_x (§6 crossover speed)"}, {"motor", "Motor (calibration record)"}}) {
        m_ident->addRow(QLatin1String(r.first), QString::fromUtf8(r.second));
    }
    m_ident->setStale(false);
    idCard->body()->addWidget(m_ident);
    grid->addWidget(idCard, 0, 0);

    auto *evCard = new Card(QStringLiteral("Arming evidence (FW-24)"));
    m_evidence = new CheckList;
    QVector<QPair<QString, QString>> items;
    const QHash<QString, QString> text = {
        {QStringLiteral("ROUTE_BOUND"), QStringLiteral("FLT_HS/LS routed to the PWM fault inputs|platform")},
        {QStringLiteral("CONFIG_MATCHES"), QStringLiteral("Fault/complementary configuration matches|platform")},
        {QStringLiteral("PROTECTION_LOCKED"), QStringLiteral("eFlexPWM REG_PROT locked|platform")},
        {QStringLiteral("FAULT_ROUTE_VALIDATED"), QStringLiteral("EOL/HIL: FLT pad → PWM fault proven|record")},
        {QStringLiteral("OVP_ROUTE_VALIDATED"), QStringLiteral("EOL/HIL: FW-06 chain ≤ 15.6 µs proven|record")}};
    for (const BitName &b : ProtocolInfo::instance().evidenceBits) {
        items.push_back({b.name, text.value(b.name, b.name + QStringLiteral("|"))});
    }
    m_evidence->setItems(items);
    evCard->body()->addWidget(m_evidence);
    auto *evHint = new QLabel(QStringLiteral("Anything missing fails closed: FS0B is never released and MCU_GATE_EN never rises."));
    evHint->setObjectName(QStringLiteral("hint"));
    evHint->setWordWrap(true);
    evCard->body()->addWidget(evHint);
    grid->addWidget(evCard, 0, 1);

    auto *calCard = new Card(QStringLiteral("Calibration record (FW-20)"));
    m_calib = new KvGrid;
    for (const auto &r : {std::pair<const char *, const char *>{"layout", "Layout version"}, {"size", "Size"}, {"err", "Errors (CAL_ERR_*)"},
                          {"motor", "Motor ID"}, {"fsw", "f_sw"}, {"serial", "Card serial"}, {"crc", "CRC-32"},
                          {"isns", "Current gains (mV/A)"}, {"vdc", "V_DC gains"}, {"rslv", "Resolver pp / zero"}}) {
        m_calib->addRow(QLatin1String(r.first), QString::fromUtf8(r.second));
    }
    m_calib->setStale(false);
    calCard->body()->addWidget(m_calib);
    m_layout = new QTableWidget(0, 3);
    m_layout->setHorizontalHeaderLabels({QStringLiteral("Field"), QStringLiteral("Offset"), QStringLiteral("Size")});
    m_layout->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_layout->verticalHeader()->hide();
    m_layout->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_layout->setMinimumHeight(200);
    m_layout->setShowGrid(false);
    calCard->body()->addWidget(m_layout);
    grid->addWidget(calCard, 1, 0, 2, 1);

    auto *valCard = new Card(QStringLiteral("EOL/HIL validation record (FW-24)"));
    m_valid = new KvGrid;
    for (const auto &r : {std::pair<const char *, const char *>{"present", "Present"}, {"fw", "For image"}, {"sku", "For SKU"},
                          {"serial", "For card"}, {"flags", "Proves"}, {"ovp", "FW-06 chain measured"}, {"accepted", "Accepted by this image"}}) {
        m_valid->addRow(QLatin1String(r.first), QString::fromUtf8(r.second));
    }
    m_valid->setStale(false);
    valCard->body()->addWidget(m_valid);
    grid->addWidget(valCard, 1, 1);

    auto *stCard = new Card(QStringLiteral("Self-test record (FW-16) and provisioning"));
    m_self = new KvGrid;
    for (const auto &r : {std::pair<const char *, const char *>{"present", "Stored pass"}, {"kc", "From key cycle"},
                          {"step", "Failed step"}, {"prov", "Provisioning (simulator)"}}) {
        m_self->addRow(QLatin1String(r.first), QString::fromUtf8(r.second));
    }
    m_self->setStale(false);
    stCard->body()->addWidget(m_self);
    grid->addWidget(stCard, 2, 1);

    auto *upCard = new Card(QStringLiteral("Firmware update (FW-38)"));
    m_upPill = makePill(QStringLiteral("NO IMAGE"), Theme::Sev::Stale);
    upCard->addHeaderWidget(m_upPill);
    auto *up = new QLabel(QStringLiteral(
        "A signed .tifw container (firmware/tools/sign-image.mjs): a 128-byte header — the target SKU, TI_FW_ID, a security "
        "version, the payload's SHA-256 — signed with Ed25519 by the release key. The firmware checks the signature, the target, "
        "the security version against its anti-rollback counter and the payload before it activates anything; the bootloader "
        "checks it again before installing it and restores the last known good image if the new one's first boot fails. The "
        "release key never enters this tool: it sends what was signed elsewhere and signs nothing. The programming session "
        "forbids arming until the next power-up (DTC_FW_UPDATE)."));
    up->setWordWrap(true);
    up->setObjectName(QStringLiteral("muted"));
    upCard->body()->addWidget(up);
    auto *buttons = new QHBoxLayout;
    auto *pick = new QPushButton(QStringLiteral("Select image…"));
    pick->setAccessibleName(QStringLiteral("firmwareSelect"));
    m_update = new QPushButton(QStringLiteral("Update…"));
    m_update->setObjectName(QStringLiteral("danger"));
    m_update->setAccessibleName(QStringLiteral("firmwareUpdate"));
    buttons->addWidget(pick);
    buttons->addWidget(m_update);
    buttons->addStretch(1);
    upCard->body()->addLayout(buttons);
    m_warn = new QLabel;
    m_warn->setObjectName(QStringLiteral("pill"));
    m_warn->setWordWrap(true);
    m_warn->hide();
    upCard->body()->addWidget(m_warn);
    auto *cols = new QGridLayout;
    cols->setSpacing(Theme::S4);
    m_imgGrid = new KvGrid;
    for (const auto &r : {std::pair<const char *, const char *>{"file", "Image"}, {"target", "Signed for"}, {"fw", "TI_FW_ID"},
                          {"sec", "Security version"}, {"len", "Payload"}, {"hash", "SHA-256 of the payload"}, {"sig", "Ed25519 signature"}}) {
        m_imgGrid->addRow(QLatin1String(r.first), QString::fromUtf8(r.second), QString(), true);
    }
    m_runGrid = new KvGrid;
    for (const auto &r : {std::pair<const char *, const char *>{"fw", "Running TI_FW_ID (DID 0xFD20)"}, {"sku", "SKU: parameters · HW_ID · record"},
                          {"serial", "Card"}, {"kc", "Key cycle · uptime (DID 0xF208)"}, {"counter", "Anti-rollback counter"},
                          {"rec", "Boot record"}}) {
        m_runGrid->addRow(QLatin1String(r.first), QString::fromUtf8(r.second), QString(), true);
    }
    m_progress = new KvGrid;
    for (const auto &r : {std::pair<const char *, const char *>{"step", "Step"}, {"blocks", "TransferData blocks"}, {"bytes", "Bytes acknowledged"},
                          {"block", "Block length granted (0x34)"}, {"time", "Elapsed · transfer"}, {"busy", "Repeated on NRC 0x21"},
                          {"verify", "Verification (0xFF01)"}, {"tp", "ISO 15765-2 (TX_DL · BS · STmin)"}}) {
        m_progress->addRow(QLatin1String(r.first), QString::fromUtf8(r.second), QString(), true);
    }
    for (KvGrid *g : {m_imgGrid, m_runGrid, m_progress}) {
        g->setStale(false);
    }
    cols->addWidget(m_imgGrid, 0, 0);
    cols->addWidget(m_runGrid, 0, 1);
    cols->addWidget(m_progress, 0, 2);
    upCard->body()->addLayout(cols);
    m_bar = new QProgressBar;
    m_bar->setTextVisible(false);
    upCard->body()->addWidget(m_bar);
    m_outcome = new QLabel;
    m_outcome->setObjectName(QStringLiteral("pill"));
    m_outcome->setWordWrap(true);
    m_outcome->hide();
    upCard->body()->addWidget(m_outcome);
    grid->addWidget(upCard, 3, 0, 1, 2);
    connect(pick, &QPushButton::clicked, this, &FirmwarePage::selectImage);
    connect(m_update, &QPushButton::clicked, this, &FirmwarePage::confirmUpdate);
    connect(&m_up, &FirmwareUpdater::changed, this, &FirmwarePage::renderUpdate);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    root->addLayout(grid);
    root->addStretch(1);

    m_ident->setValue(QStringLiteral("exp"), ProtocolInfo::instance().fwId);
    connect(&m_s, &Session::infoChanged, this, &FirmwarePage::onInfo);
    connect(&m_s, &Session::tick, this, [this] {
        m_evidence->setStale(!m_s.isLive());
        renderUpdate(); // the elapsed time, the enables
    });
    onFrames([this](const TelemetryFrame &f) { onFrame(f); });
    connect(&m_s, &Session::transportChanged, this, [this] {
        const bool can = m_s.transport() && m_s.transport()->kind() == ITransport::Kind::Can;
        m_canNote->setVisible(can);
        setPill(m_fwPill, QStringLiteral("UNKNOWN"), Theme::Sev::Stale);
        m_evidence->setAll(CheckList::State::Unknown);
    });
}

void FirmwarePage::showEvent(QShowEvent *e)
{
    // the records change as the firmware runs (the FW-16 pass is stored after the self-test): ask the simulator again
    if (m_s.isDeviceLive() && m_s.transport()->isSimulator()) {
        m_s.command(QStringLiteral("info"));
    }
    Page::showEvent(e);
}

void FirmwarePage::onInfo(const QJsonObject &info)
{
    const ProtocolInfo &pi = ProtocolInfo::instance();
    const QString fw = info.value(QLatin1String("fw_id")).toString();
    if (!fw.isEmpty()) {
        const bool match = fw.compare(pi.fwId, Qt::CaseInsensitive) == 0;
        setPill(m_fwPill, match ? QStringLiteral("MATCHES THE EXPORTS") : QStringLiteral("OTHER IMAGE"), match ? Theme::Sev::Ok : Theme::Sev::Warn);
        m_ident->setValue(QStringLiteral("fw"), fw, match ? Theme::Sev::Ok : Theme::Sev::Warn);
    }
    m_ident->setValue(QStringLiteral("sku"), QStringLiteral("%1 (%2)").arg(info.value(QLatin1String("sku_name")).toString(QStringLiteral("—")))
                                                 .arg(info.value(QLatin1String("sku")).toInt()));
    m_ident->setValue(QStringLiteral("bridge"), info.value(QLatin1String("bridge")).toString(QStringLiteral("—")));
    m_ident->setValue(QStringLiteral("boot"), QStringLiteral("%1 / %2").arg(info.value(QLatin1String("boot")).toInt())
                                                  .arg(info.value(QLatin1String("key_cycle")).toInt()));
    m_ident->setValue(QStringLiteral("fsw"), QStringLiteral("%1 Hz / %2 Hz / %3 Hz").arg(info.value(QLatin1String("fsw_hz")).toDouble())
                                                 .arg(info.value(QLatin1String("isr_hz")).toDouble())
                                                 .arg(info.value(QLatin1String("fc_hz")).toDouble(), 0, 'f', 0));
    m_ident->setValue(QStringLiteral("nx"), QStringLiteral("%1 rpm").arg(info.value(QLatin1String("n_x_rpm")).toDouble(), 0, 'f', 0));
    const QJsonObject m = info.value(QLatin1String("motor")).toObject();
    m_ident->setValue(QStringLiteral("motor"), QStringLiteral("Ld %1 mH · Lq %2 mH · ψ %3 Wb · %4 pp")
                                                   .arg(m.value(QLatin1String("ld_h")).toDouble() * 1e3, 0, 'g', 3)
                                                   .arg(m.value(QLatin1String("lq_h")).toDouble() * 1e3, 0, 'g', 3)
                                                   .arg(m.value(QLatin1String("psi_wb")).toDouble(), 0, 'g', 3)
                                                   .arg(m.value(QLatin1String("pp")).toInt()));

    const QJsonObject c = info.value(QLatin1String("calib")).toObject();
    if (!c.isEmpty()) {
        const int err = c.value(QLatin1String("err")).toInt();
        m_calib->setValue(QStringLiteral("layout"), QString::number(c.value(QLatin1String("layout_version")).toInt()));
        m_calib->setValue(QStringLiteral("size"), QStringLiteral("%1 bytes").arg(c.value(QLatin1String("size")).toInt()));
        m_calib->setValue(QStringLiteral("err"), err ? ProtocolInfo::bitsText(err, pi.calErrBits) : QStringLiteral("none"),
                          err ? Theme::Sev::Crit : Theme::Sev::Ok);
        m_calib->setValue(QStringLiteral("motor"), hex32(c.value(QLatin1String("motor_id")).toDouble()),
                          c.value(QLatin1String("motor_id")).toDouble() == 0 ? Theme::Sev::Crit : Theme::Sev::Neutral);
        m_calib->setValue(QStringLiteral("fsw"), QStringLiteral("%1 Hz").arg(c.value(QLatin1String("fsw_hz")).toInt()));
        m_calib->setValue(QStringLiteral("serial"), c.value(QLatin1String("serial")).toString());
        m_calib->setValue(QStringLiteral("crc"), c.value(QLatin1String("crc32")).toString());
        QStringList g;
        for (const QJsonValue &x : c.value(QLatin1String("isns")).toArray()) {
            g << QString::number(x[QLatin1String("gain_v_per_a")].toDouble() * 1e3, 'f', 3);
        }
        m_calib->setValue(QStringLiteral("isns"), g.join(QStringLiteral(" · ")));
        QStringList v;
        for (const QJsonValue &x : c.value(QLatin1String("vdc")).toArray()) {
            v << QString::number(x[QLatin1String("gain")].toDouble(), 'f', 2);
        }
        m_calib->setValue(QStringLiteral("vdc"), v.join(QStringLiteral(" · ")));
        const QJsonObject r = c.value(QLatin1String("rslv")).toObject();
        m_calib->setValue(QStringLiteral("rslv"), QStringLiteral("%1 / %2 · zero %3 rad").arg(r.value(QLatin1String("motor_pp")).toInt())
                                                      .arg(r.value(QLatin1String("resolver_pp")).toInt())
                                                      .arg(r.value(QLatin1String("zero_rad")).toDouble(), 0, 'f', 3));
        const QJsonArray fields = c.value(QLatin1String("fields")).toArray();
        m_layout->setRowCount(static_cast<int>(fields.size()));
        for (int i = 0; i < fields.size(); i++) {
            const QJsonObject f = fields[i].toObject();
            m_layout->setItem(i, 0, new QTableWidgetItem(f.value(QLatin1String("name")).toString()));
            for (int k = 1; k < 3; k++) {
                auto *it = new QTableWidgetItem(QString::number(f.value(QLatin1String(k == 1 ? "offset" : "size")).toInt()));
                it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                m_layout->setItem(i, k, it);
            }
        }
    }
    const QJsonObject v = info.value(QLatin1String("validation")).toObject();
    if (!v.isEmpty()) {
        const bool present = v.value(QLatin1String("present")).toBool();
        m_valid->setValue(QStringLiteral("present"), yesNo(v.value(QLatin1String("present"))), present ? Theme::Sev::Ok : Theme::Sev::Crit);
        const QString vfw = v.value(QLatin1String("fw_id")).toString(QStringLiteral("—"));
        m_valid->setValue(QStringLiteral("fw"), vfw, (vfw == info.value(QLatin1String("fw_id")).toString()) ? Theme::Sev::Ok : Theme::Sev::Warn);
        m_valid->setValue(QStringLiteral("sku"), v.contains(QLatin1String("sku")) ? QString::number(v.value(QLatin1String("sku")).toInt()) : QStringLiteral("—"));
        m_valid->setValue(QStringLiteral("serial"), v.value(QLatin1String("serial")).toString(QStringLiteral("—")));
        m_valid->setValue(QStringLiteral("flags"), ProtocolInfo::bitsText(v.value(QLatin1String("flags")).toInt(), pi.evidenceBits));
        const double ns = v.value(QLatin1String("ovp_chain_ns")).toDouble(-1), budget = v.value(QLatin1String("budget_us")).toDouble(15.6);
        m_valid->setValue(QStringLiteral("ovp"), ns < 0 ? QStringLiteral("—") : QStringLiteral("%1 µs (budget %2 µs)").arg(ns / 1000.0, 0, 'f', 2).arg(budget),
                          ns < 0 ? Theme::Sev::Neutral : (ns / 1000.0 <= budget ? Theme::Sev::Ok : Theme::Sev::Crit));
        const int acc = v.value(QLatin1String("valid_flags")).toInt();
        m_valid->setValue(QStringLiteral("accepted"), ProtocolInfo::bitsText(acc, pi.evidenceBits), (acc & 0x18) == 0x18 ? Theme::Sev::Ok : Theme::Sev::Crit);
    }
    const QJsonObject st = info.value(QLatin1String("selftest_record")).toObject();
    if (!st.isEmpty()) {
        m_self->setValue(QStringLiteral("present"), st.value(QLatin1String("present")).toBool() && st.value(QLatin1String("passed")).toBool()
                                                         ? QStringLiteral("yes") : QStringLiteral("no"));
        m_self->setValue(QStringLiteral("kc"), st.contains(QLatin1String("key_cycle")) ? QString::number(st.value(QLatin1String("key_cycle")).toInt()) : QStringLiteral("—"));
        m_self->setValue(QStringLiteral("step"), st.contains(QLatin1String("failed_step")) ? QString::number(st.value(QLatin1String("failed_step")).toInt()) : QStringLiteral("—"));
    }
    const QJsonObject pv = info.value(QLatin1String("provision")).toObject();
    if (!pv.isEmpty()) {
        QStringList off;
        for (const char *k : {"val", "route", "otp", "cal"}) {
            if (!pv.value(QLatin1String(k)).toBool()) {
                off << QLatin1String(k);
            }
        }
        m_self->setValue(QStringLiteral("prov"), off.isEmpty() ? QStringLiteral("complete") : QStringLiteral("withdrawn: %1").arg(off.join(QStringLiteral(", "))),
                         off.isEmpty() ? Theme::Sev::Ok : Theme::Sev::Warn);
    }
}

void FirmwarePage::onFrame(const TelemetryFrame &f)
{
    int present = -1;
    if (f.has(QStringLiteral("state.evidence"))) {
        present = static_cast<int>(f.v(QStringLiteral("state.evidence")));
    } else if (f.has(QStringLiteral("inv.evidence_missing"))) {
        present = 0x1F & ~static_cast<int>(f.v(QStringLiteral("inv.evidence_missing")));
    }
    if (present < 0) {
        return;
    }
    for (const BitName &b : ProtocolInfo::instance().evidenceBits) {
        m_evidence->set(b.name, (present & b.bit) ? CheckList::State::Ok : CheckList::State::Fail);
    }
}

// ---------------- the FW-38 update ----------------
bool FirmwarePage::loadImage(const QString &path)
{
    m_img = FirmwareUpdater::load(path);
    m_up.readIdentity(); // the comparison needs the running image (renders when read)
    renderUpdate();
    return m_img.ok();
}

void FirmwarePage::selectImage()
{
    QSettings st;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Select a FW-38 image"), st.value(QStringLiteral("firmware/dir")).toString(),
                                                      QStringLiteral("FW-38 images (*.tifw);;All files (*)"));
    if (!path.isEmpty()) {
        st.setValue(QStringLiteral("firmware/dir"), QFileInfo(path).absolutePath());
        loadImage(path);
    }
}

// The running image read again, then everything that will happen and every warning, behind a typed confirmation.
void FirmwarePage::confirmUpdate()
{
    const auto confirm = [this] {
        const FirmwareUpdater::Identity &id = m_up.identity();
        QString t = QStringLiteral("<b>%1</b>: %2, TI_FW_ID %3, security version %4, %5 bytes.<br>")
                        .arg(QFileInfo(m_img.path).fileName().toHtmlEscaped(), FirmwareUpdater::skuName(static_cast<int>(m_img.target)),
                             fwHex(m_img.fwId)).arg(m_img.secVer).arg(m_img.bytes.size());
        t += id.valid ? QStringLiteral("Running: TI_FW_ID %1, %2, card %3.<br>").arg(fwHex(id.fwId), FirmwareUpdater::skuName(id.sku), id.serial.toHtmlEscaped())
                      : QStringLiteral("The running image is unknown.<br>");
        for (const QString &w : m_up.warnings(m_img)) {
            t += QStringLiteral("⚠ %1<br>").arg(w.toHtmlEscaped());
        }
        t += QStringLiteral("<br>The tool unlocks (SecurityAccess), enters the programming session — the inverter cannot arm until its "
                            "next power-up — downloads the image, has the firmware verify it, activates it and resets the ECU. After a "
                            "new TI_FW_ID the inverter does not arm until an EOL/HIL validation record exists for it (FW-24).");
        if (ControlsPage::confirmTyped(this, QStringLiteral("Update the firmware"), t, QStringLiteral("UPDATE"))) {
            m_up.start(m_img); // a refusal to begin is in text()
        }
        renderUpdate();
    };
    // defer the dialog out of the transport's callback (its event loop must not run inside one)
    if (!m_up.readIdentity([this, confirm] { QTimer::singleShot(0, this, confirm); })) {
        confirm();
    }
}

void FirmwarePage::renderUpdate()
{
    using Sev = Theme::Sev;
    using U = FirmwareUpdater;
    const U::Identity &id = m_up.after().valid ? m_up.after() : m_up.identity(); // what runs now
    const bool have = !m_img.path.isEmpty();
    m_imgGrid->setValue(QStringLiteral("file"), have ? QStringLiteral("%1 · %2 bytes").arg(QFileInfo(m_img.path).fileName()).arg(m_img.bytes.size()) : QStringLiteral("—"));
    const bool hdr = have && m_img.bytes.size() >= U::HDR_LEN;
    m_imgGrid->setValue(QStringLiteral("target"), hdr ? U::skuName(static_cast<int>(m_img.target)) : QStringLiteral("—"),
                        hdr && id.valid && static_cast<int>(m_img.target) != id.sku ? Sev::Crit : Sev::Neutral);
    m_imgGrid->setValue(QStringLiteral("fw"), hdr ? fwHex(m_img.fwId) : QStringLiteral("—"), hdr && id.valid && m_img.fwId <= id.fwId ? Sev::Warn : Sev::Neutral);
    const qint64 counter = id.bootRec.contains(QLatin1String("sec_counter")) ? id.bootRec.value(QLatin1String("sec_counter")).toInteger() : -1;
    m_imgGrid->setValue(QStringLiteral("sec"), hdr ? QString::number(m_img.secVer) : QStringLiteral("—"),
                        hdr && counter >= 0 && m_img.secVer < counter ? Sev::Crit : Sev::Neutral);
    m_imgGrid->setValue(QStringLiteral("len"), hdr ? QStringLiteral("%1 bytes after the 128-byte header").arg(m_img.length) : QStringLiteral("—"));
    m_imgGrid->setValue(QStringLiteral("hash"), !hdr ? QStringLiteral("—") : m_img.hashOk ? QStringLiteral("matches the header") : QStringLiteral("does not match the header"),
                        !hdr ? Sev::Neutral : m_img.hashOk ? Sev::Ok : Sev::Crit);
    m_imgGrid->setValue(QStringLiteral("sig"), !hdr ? QStringLiteral("—") : m_img.signature ? QStringLiteral("present — the firmware verifies it") : QStringLiteral("absent"),
                        !hdr || m_img.signature ? Sev::Neutral : Sev::Crit);

    m_runGrid->setValue(QStringLiteral("fw"), id.valid ? fwHex(id.fwId) : (id.error.isEmpty() ? QStringLiteral("—") : id.error));
    m_runGrid->setValue(QStringLiteral("sku"), id.valid ? QStringLiteral("%1 · %2 · %3").arg(U::skuName(id.sku), U::skuName(id.hwSku), U::skuName(id.calSku)) : QStringLiteral("—"));
    m_runGrid->setValue(QStringLiteral("serial"), id.valid ? id.serial : QStringLiteral("—"));
    m_runGrid->setValue(QStringLiteral("kc"), id.haveUptime ? QStringLiteral("%1 · %2 s").arg(id.keyCycle).arg(id.uptimeMs / 1000.0, 0, 'f', 1) : QStringLiteral("—"));
    m_runGrid->setValue(QStringLiteral("counter"), counter >= 0 ? QStringLiteral("%1 (DID 0xFD24)").arg(counter)
                                                   : id.valid ? QStringLiteral("not reported — the firmware judges it (ROLLBACK)") : QStringLiteral("—"));
    const QJsonObject &rec = id.bootRec;
    m_runGrid->setValue(QStringLiteral("rec"), rec.isEmpty() ? (id.valid ? QStringLiteral("none reported (DID 0xFD24)") : QStringLiteral("—"))
                                                             : QStringLiteral("%1 · last %2 · last known good %3").arg(U::bootStateName(rec.value(QLatin1String("state")).toInt()),
                                                                                                                      U::bootLastName(rec.value(QLatin1String("last")).toInt()),
                                                                                                                      rec.value(QLatin1String("lkg_valid")).toBool() ? QStringLiteral("yes") : QStringLiteral("no")));

    const QStringList w = !have ? QStringList() : m_img.ok() ? m_up.warnings(m_img) : QStringList{m_img.error};
    setPill(m_warn, w.join(QLatin1Char('\n')), m_img.ok() ? Sev::Warn : Sev::Crit);
    m_warn->setVisible(!w.isEmpty());

    m_progress->setValue(QStringLiteral("step"), U::stepName(m_up.step()), m_up.step() == U::Step::Failed ? Sev::Crit : m_up.step() == U::Step::Done ? Sev::Ok : Sev::Neutral);
    m_progress->setValue(QStringLiteral("blocks"), m_up.blocksTotal() ? QStringLiteral("%1 / %2").arg(m_up.blocksDone()).arg(m_up.blocksTotal()) : QStringLiteral("—"));
    m_progress->setValue(QStringLiteral("bytes"), m_up.bytesTotal() && m_up.step() != U::Step::Idle ? QStringLiteral("%1 / %2").arg(m_up.bytesSent()).arg(m_up.bytesTotal()) : QStringLiteral("—"));
    m_progress->setValue(QStringLiteral("block"), m_up.blockMax() ? QStringLiteral("%1 bytes (%2 data)").arg(m_up.blockMax()).arg(m_up.blockMax() - 2) : QStringLiteral("—"));
    m_progress->setValue(QStringLiteral("time"), m_up.elapsedMs() < 0 ? QStringLiteral("—")
                                                 : QStringLiteral("%1 s · %2").arg(m_up.elapsedMs() / 1000.0, 0, 'f', 1)
                                                       .arg(m_up.transferMs() < 0 ? QStringLiteral("—") : QStringLiteral("%1 s").arg(m_up.transferMs() / 1000.0, 0, 'f', 2)));
    m_progress->setValue(QStringLiteral("busy"), QString::number(m_up.busyRepeats()), m_up.busyRepeats() ? Sev::Warn : Sev::Neutral);
    const int v = m_up.verifyStatus();
    m_progress->setValue(QStringLiteral("verify"), v < 0 ? QStringLiteral("—") : v == 0 ? QStringLiteral("running") : v == 1 ? QStringLiteral("passed")
                                                                                 : QStringLiteral("refused: %1").arg(U::imgResultText(m_up.verifyReason())),
                         v == 1 ? Sev::Ok : v == 2 ? Sev::Crit : Sev::Neutral);
    const QJsonObject &tp = m_up.transport();
    m_progress->setValue(QStringLiteral("tp"), tp.isEmpty() ? QStringLiteral("—")
                                                            : QStringLiteral("%1 · BS %2 · STmin %3 · the last block in %4 frames").arg(tp.value(QLatin1String("tx_dl")).toInt())
                                                                  .arg(tp.value(QLatin1String("fc_bs")).toInt()).arg(tp.value(QLatin1String("fc_stmin")).toInt())
                                                                  .arg(tp.value(QLatin1String("tx_frames")).toInt()));
    m_bar->setRange(0, static_cast<int>(std::max<qint64>(1, m_up.bytesTotal())));
    m_bar->setValue(static_cast<int>(m_up.bytesSent()));
    const bool ran = m_up.step() == U::Step::Done || m_up.step() == U::Step::Failed || (!m_up.busy() && !m_up.text().isEmpty());
    setPill(m_outcome, m_up.text(), m_up.step() == U::Step::Done ? Sev::Ok : m_up.step() == U::Step::Failed ? Sev::Crit : Sev::Warn);
    m_outcome->setVisible(ran);
    setPill(m_upPill, m_up.busy() ? U::stepName(m_up.step()).toUpper() : m_up.step() == U::Step::Done ? QStringLiteral("DONE")
                      : m_up.step() == U::Step::Failed ? QStringLiteral("FAILED") : have ? (m_img.ok() ? QStringLiteral("IMAGE READY") : QStringLiteral("IMAGE UNUSABLE"))
                                                                                        : QStringLiteral("NO IMAGE"),
            m_up.busy() ? Sev::Accent : m_up.step() == U::Step::Done ? Sev::Ok : m_up.step() == U::Step::Failed ? Sev::Crit : have && m_img.ok() ? Sev::Info : Sev::Stale);

    ITransport *t = m_s.transport();
    const QString why = !m_img.ok() || !have                            ? QStringLiteral("select a usable image")
                      : m_up.busy()                                    ? QStringLiteral("an update is in progress")
                      : m_up.reading()                                 ? QStringLiteral("reading the running image")
                      : !m_s.isDeviceLive()                            ? QStringLiteral("no live link to the inverter")
                      : !t->commands().contains(QStringLiteral("uds")) ? QStringLiteral("this transport has no UDS")
                      : (t->kind() == ITransport::Kind::Can && !m_s.serviceUnlocked()) ? QStringLiteral("the service key (Tools ▸ Service key)")
                                                                       : QString();
    m_update->setEnabled(why.isEmpty());
    m_update->setToolTip(why.isEmpty() ? QStringLiteral("SecurityAccess, the programming session, download, verify, activate, ECUReset (typed confirmation)")
                                       : QStringLiteral("Not now: %1").arg(why));
}
