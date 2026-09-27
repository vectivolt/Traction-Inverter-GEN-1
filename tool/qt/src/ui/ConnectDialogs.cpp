#include "ConnectDialogs.h"

#include "ProtocolInfo.h"
#include "Theme.h"

#include <QCanBus>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardItemModel>

SimulatorDialog::SimulatorDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Start the simulator"));
    QSettings s;
    auto *form = new QFormLayout(this);
    form->setContentsMargins(Theme::S5, Theme::S5, Theme::S5, Theme::S4);
    form->setSpacing(Theme::S3);
    auto *intro = new QLabel(QStringLiteral("The simulator bridge runs the firmware's host build, unmodified, with a PMSM on a dyno, "
                                            "the DC link, a bench VCU/BMS and fault injection (tool/PROTOCOL.md)."));
    intro->setWordWrap(true);
    intro->setObjectName(QStringLiteral("muted"));
    form->addRow(intro);
    auto *row = new QHBoxLayout;
    m_path = new QLineEdit(s.value(QStringLiteral("sim/path"), BridgeTransport::locate()).toString());
    auto *browse = new QPushButton(QStringLiteral("Browse…"));
    row->addWidget(m_path, 1);
    row->addWidget(browse);
    form->addRow(QStringLiteral("Bridge"), row);
    m_sku = new QComboBox;
    for (const SkuInfo &k : ProtocolInfo::instance().skus) {
        m_sku->addItem(k.name, k.key);
    }
    m_sku->setCurrentIndex(std::max(0, m_sku->findData(s.value(QStringLiteral("sim/sku"), QStringLiteral("8xx_sic")))));
    form->addRow(QStringLiteral("SKU"), m_sku);
    m_rate = new QSpinBox;
    m_rate->setRange(1, 1000);
    m_rate->setValue(s.value(QStringLiteral("sim/rate"), 100).toInt());
    m_rate->setSuffix(QStringLiteral(" Hz (simulated time)"));
    form->addRow(QStringLiteral("Telemetry"), m_rate);
    m_time = new QDoubleSpinBox;
    m_time->setRange(0, 100);
    m_time->setDecimals(2);
    m_time->setValue(s.value(QStringLiteral("sim/time"), 1.0).toDouble());
    m_time->setPrefix(QStringLiteral("× "));
    m_time->setToolTip(QStringLiteral("Simulated seconds per wall second; 0 = as fast as possible"));
    form->addRow(QStringLiteral("Time factor"), m_time);
    m_paused = new QCheckBox(QStringLiteral("Start paused (advance with step)"));
    form->addRow(QString(), m_paused);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    form->addRow(m_status);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Start"));
    bb->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("primary"));
    form->addRow(bb);
    auto check = [this, bb] {
        const QFileInfo fi(m_path->text());
        const bool ok = fi.isFile() && fi.isExecutable();
        m_status->setText(ok ? QString() : QStringLiteral("Not an executable. Build it with  make -C tool/bridge  or point to sim_bridge."));
        m_status->setObjectName(ok ? QStringLiteral("muted") : QStringLiteral("critText"));
        m_status->style()->unpolish(m_status);
        m_status->style()->polish(m_status);
        bb->button(QDialogButtonBox::Ok)->setEnabled(ok);
    };
    connect(m_path, &QLineEdit::textChanged, this, check);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString p = QFileDialog::getOpenFileName(this, QStringLiteral("Simulator bridge"), m_path->text());
        if (!p.isEmpty()) {
            m_path->setText(p);
        }
    });
    connect(bb, &QDialogButtonBox::accepted, this, [this] {
        QSettings st;
        st.setValue(QStringLiteral("sim/path"), m_path->text());
        st.setValue(QStringLiteral("sim/sku"), m_sku->currentData());
        st.setValue(QStringLiteral("sim/rate"), m_rate->value());
        st.setValue(QStringLiteral("sim/time"), m_time->value());
        accept();
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    check();
    setMinimumWidth(560);
}

BridgeTransport::Options SimulatorDialog::options() const
{
    BridgeTransport::Options o;
    o.program = m_path->text();
    o.sku = m_sku->currentData().toString();
    o.rateHz = m_rate->value();
    o.timeFactor = m_time->value();
    o.paused = m_paused->isChecked();
    return o;
}

QVector<QPair<QString, bool>> CanDialog::plugins()
{
    const QStringList have = QCanBus::instance()->plugins();
    // the adapters Qt SerialBus ships plugins for (availability depends on the platform and the vendor driver)
    QStringList known = {QStringLiteral("socketcan"), QStringLiteral("peakcan"), QStringLiteral("kvaser"),
                         QStringLiteral("vectorcan"), QStringLiteral("systeccan"), QStringLiteral("tinycan"),
                         QStringLiteral("passthrucan"), QStringLiteral("virtualcan")};
    for (const QString &p : have) {
        if (!known.contains(p)) {
            known << p;
        }
    }
    QVector<QPair<QString, bool>> out;
    for (const QString &p : known) {
        out.push_back({p, have.contains(p)});
    }
    return out;
}

QString CanDialog::hint(const QString &plugin)
{
    static const QHash<QString, QString> h = {
        {QStringLiteral("socketcan"), QStringLiteral("Linux SocketCAN (can0, vcan0 …): any adapter with a kernel driver, Kvaser and PEAK included")},
        {QStringLiteral("peakcan"), QStringLiteral("PEAK-System PCAN-Basic: needs the PCAN-Basic library from PEAK (macOS: PCBUSB)")},
        {QStringLiteral("kvaser"), QStringLiteral("Qt SerialBus has no Kvaser plugin: use Kvaser's SocketCAN driver (Linux) or its J2534 driver through passthrucan (Windows)")},
        {QStringLiteral("vectorcan"), QStringLiteral("Vector XL Driver Library (Windows): needs vxlapi from Vector")},
        {QStringLiteral("systeccan"), QStringLiteral("SYS TEC USB-CANmodul (Windows)")},
        {QStringLiteral("tinycan"), QStringLiteral("MHS Tiny-CAN: needs the MHS driver library")},
        {QStringLiteral("passthrucan"), QStringLiteral("SAE J2534 PassThru adapters (Windows): the vendor's J2534 DLL")},
        {QStringLiteral("virtualcan"), QStringLiteral("Qt's virtual bus between processes on this machine (a local TCP loopback): for tests")},
    };
    return h.value(plugin);
}

CanDialog::CanDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Connect a CAN adapter"));
    QSettings s;
    auto *form = new QFormLayout(this);
    form->setContentsMargins(Theme::S5, Theme::S5, Theme::S5, Theme::S4);
    form->setSpacing(Theme::S3);
    auto *intro = new QLabel(QStringLiteral("Listens to INV_STATUS (0x201, 20 bytes, CAN FD) with the E2E checks the firmware applies. "
                                            "Nothing is transmitted until the service key is entered (Tools ▸ Service key)."));
    intro->setWordWrap(true);
    intro->setObjectName(QStringLiteral("muted"));
    form->addRow(intro);
    m_plugin = new QComboBox;
    auto *model = qobject_cast<QStandardItemModel *>(m_plugin->model());
    for (const auto &p : plugins()) {
        m_plugin->addItem(p.second ? p.first : QStringLiteral("%1 (not on this system)").arg(p.first), p.first);
        m_plugin->setItemData(m_plugin->count() - 1, hint(p.first), Qt::ToolTipRole);
        if (!p.second && model) {
            model->item(m_plugin->count() - 1)->setEnabled(false);
        }
    }
    const int saved = m_plugin->findData(s.value(QStringLiteral("can/plugin")));
    for (int i = 0; i < m_plugin->count(); i++) {
        if ((saved >= 0 && i == saved) || (saved < 0 && model && model->item(i)->isEnabled())) {
            m_plugin->setCurrentIndex(i);
            break;
        }
    }
    form->addRow(QStringLiteral("Plugin"), m_plugin);
    m_iface = new QComboBox;
    m_iface->setEditable(true);
    form->addRow(QStringLiteral("Interface"), m_iface);
    m_bitrate = new QComboBox;
    for (int b : {125000, 250000, 500000, 1000000}) {
        m_bitrate->addItem(QStringLiteral("%1 kbit/s").arg(b / 1000), b);
    }
    m_bitrate->setCurrentIndex(std::max(0, m_bitrate->findData(s.value(QStringLiteral("can/bitrate"), 500000))));
    form->addRow(QStringLiteral("Nominal bit rate"), m_bitrate);
    m_fd = new QCheckBox(QStringLiteral("CAN FD (INV_STATUS is 20 bytes)"));
    m_fd->setChecked(s.value(QStringLiteral("can/fd"), true).toBool());
    form->addRow(QString(), m_fd);
    m_dataBitrate = new QComboBox;
    for (int b : {1000000, 2000000, 4000000, 5000000, 8000000}) {
        m_dataBitrate->addItem(QStringLiteral("%1 Mbit/s").arg(b / 1000000), b);
    }
    m_dataBitrate->setCurrentIndex(std::max(0, m_dataBitrate->findData(s.value(QStringLiteral("can/databitrate"), 2000000))));
    form->addRow(QStringLiteral("Data bit rate"), m_dataBitrate);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setObjectName(QStringLiteral("muted"));
    form->addRow(m_status);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Connect"));
    bb->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("primary"));
    form->addRow(bb);
    connect(m_plugin, &QComboBox::currentIndexChanged, this, &CanDialog::fillInterfaces);
    connect(m_fd, &QCheckBox::toggled, m_dataBitrate, &QWidget::setEnabled);
    connect(bb, &QDialogButtonBox::accepted, this, [this] {
        QSettings st;
        st.setValue(QStringLiteral("can/plugin"), m_plugin->currentData());
        st.setValue(QStringLiteral("can/bitrate"), m_bitrate->currentData());
        st.setValue(QStringLiteral("can/fd"), m_fd->isChecked());
        st.setValue(QStringLiteral("can/databitrate"), m_dataBitrate->currentData());
        accept();
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    fillInterfaces();
    m_dataBitrate->setEnabled(m_fd->isChecked());
    const bool any = std::any_of(plugins().begin(), plugins().end(), [](const auto &p) { return p.second; });
    bb->button(QDialogButtonBox::Ok)->setEnabled(any);
    setMinimumWidth(520);
}

void CanDialog::fillInterfaces()
{
    m_status->setToolTip(QString());
    m_iface->clear();
    const QString plugin = m_plugin->currentData().toString();
    QString err;
    const QList<QCanBusDeviceInfo> devs = QCanBus::instance()->availableDevices(plugin, &err);
    m_status->setToolTip(err); // the full driver message; the label keeps its first clause
    for (const QCanBusDeviceInfo &d : devs) {
        m_iface->addItem(QStringLiteral("%1%2").arg(d.name(), d.description().isEmpty() ? QString() : QStringLiteral(" — ") + d.description()), d.name());
    }
    if (plugin == QLatin1String("virtualcan") && devs.isEmpty()) {
        m_iface->addItem(QStringLiteral("can0"), QStringLiteral("can0"));
    }
    m_status->setText(QStringLiteral("%1\n%2").arg(hint(plugin),
                          devs.isEmpty() ? QStringLiteral("No interface reported by %1%2 — type one if you know its name.")
                                               .arg(plugin, err.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(err.section(QLatin1Char(':'), 0, 0).left(120)))
                                         : QStringLiteral("%1 interface(s) found.").arg(devs.size())));
}

CanTransport::Options CanDialog::options() const
{
    CanTransport::Options o;
    o.plugin = m_plugin->currentData().toString();
    o.interface = m_iface->currentText().section(QStringLiteral(" — "), 0, 0).trimmed(); // listed or typed
    o.bitrate = m_bitrate->currentData().toInt();
    o.fd = m_fd->isChecked();
    o.dataBitrate = m_dataBitrate->currentData().toInt();
    return o;
}
