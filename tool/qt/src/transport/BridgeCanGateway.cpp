#include "BridgeCanGateway.h"

#include "CanCodec.h"

#include <QCanBusFrame>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
BridgeTransport::Options realTime(BridgeTransport::Options o)
{
    o.timeFactor = 1.0;
    o.paused = false;
    return o;
}
} // namespace

BridgeCanGateway::BridgeCanGateway(BridgeTransport::Options bridge, QCanBusDevice *device, QObject *parent)
    : QObject(parent), m_bridge(realTime(std::move(bridge))), m_dev(device)
{
    m_dev->setParent(this);
    connect(m_dev, &QCanBusDevice::framesReceived, this, &BridgeCanGateway::toFirmware);
    connect(m_dev, &QCanBusDevice::stateChanged, this, [this](QCanBusDevice::CanBusDeviceState s) {
        if (s == QCanBusDevice::ConnectedState) {
            m_bridge.open(); // the firmware starts once its frames have somewhere to go
        }
    });
    connect(m_dev, &QCanBusDevice::errorOccurred, this, [this](QCanBusDevice::CanBusError e) {
        if (e == QCanBusDevice::ConnectionError) {
            Q_EMIT failed(QStringLiteral("CAN: %1").arg(m_dev->errorString()));
        }
    });
    connect(&m_bridge, &ITransport::traffic, this, &BridgeCanGateway::toBus);
    connect(&m_bridge, &ITransport::stateChanged, this, [this](ITransport::State s, const QString &detail) {
        if (s == ITransport::State::Open) { // an external VCU and BMS; every frame the firmware sends
            m_bridge.send(QStringLiteral("vcu_model"),
                          {{QStringLiteral("cmd_frames"), false}, {QStringLiteral("bms_frames"), false}});
            m_bridge.send(QStringLiteral("can_tap"), {{QStringLiteral("on"), true}});
        } else if (s == ITransport::State::Failed) {
            Q_EMIT failed(detail);
        }
    });
    connect(&m_bridge, &ITransport::ack, this, [this](const Ack &a) {
        if (!a.ok && a.cmd == QLatin1String("can_rx")) {
            m_stats.refused++;
        }
    });
}

BridgeCanGateway::~BridgeCanGateway()
{
    m_bridge.disconnect(this); // its shutdown (the member's destructor) emits nothing into a half-destroyed gateway
}

void BridgeCanGateway::start()
{
    if (!m_dev->connectDevice()) {
        Q_EMIT failed(QStringLiteral("CAN: %1").arg(m_dev->errorString()));
    }
}

// A frame the firmware sent (a bridge "can" line) onto the bus.
void BridgeCanGateway::toBus(const QString &direction, const QString &line)
{
    if (direction != QLatin1String("can")) {
        return;
    }
    const QJsonObject o = QJsonDocument::fromJson(line.toUtf8()).object();
    QCanBusFrame f(static_cast<QCanBusFrame::FrameId>(o.value(QLatin1String("id")).toInt()),
                   QByteArray::fromHex(o.value(QLatin1String("hex")).toString().toLatin1()));
    f.setFlexibleDataRateFormat(true);
    f.setBitrateSwitch(true);
    (f.isValid() && m_dev->writeFrame(f)) ? m_stats.toBus++ : m_stats.dropped++;
}

// Frames from the bus into the firmware, the identifier choosing its bus.
void BridgeCanGateway::toFirmware()
{
    while (m_dev->framesAvailable() > 0) {
        const QCanBusFrame f = m_dev->readFrame();
        if (f.frameType() != QCanBusFrame::DataFrame || f.hasExtendedFrameFormat()) {
            m_stats.dropped++; // the firmware's buses carry 11-bit data frames only
            continue;
        }
        const quint32 id = f.frameId();
        const bool diag = id == CanCodec::ID_UDS_REQ || id == CanCodec::ID_UDS_RSP || id == CanCodec::ID_UDS_PERIODIC;
        m_bridge.send(QStringLiteral("can_rx"), {{QStringLiteral("can_id"), static_cast<int>(id)},
                                                 {QStringLiteral("hex"), QString::fromLatin1(f.payload().toHex())},
                                                 {QStringLiteral("bus"), diag ? 1 : 0}});
        m_stats.toFirmware++;
    }
}
