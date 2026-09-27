// BridgeCanGateway — the simulator bridge (tool/bridge/sim_bridge: the real firmware on its plant) on a CAN bus, so that
// CanTransport meets the firmware through a real QCanBusDevice without hardware. Every frame the firmware transmits (the
// bridge's can_tap, both buses) is written to the device as the target sends it: CAN FD with bit-rate switch
// (firmware/src/platform/s32k396/s32k396_io.c hal_can_tx). Every data frame received from the device goes into the
// firmware (can_rx): onto the diagnostic bus for the diagnostic identifiers 0x7E1/0x7E9/0x6E9 (PROTOCOL.md B.6),
// otherwise onto the vehicle bus — both firmware buses share the one device, as CanTransport has one adapter.
// The bridge's own VCU and BMS relay are silenced (vcu_model): the VCU on the bus drives the firmware, and — as the
// vehicle wires them — the contactor field of its VCU_CMD drives the plant's contactors (A.4.2), so that VCU reports
// precharge, then closed once the link is charged (CanTransport: "contactors"). Plant, dyno and fault commands go to
// bridge() as JSON, as on the simulator; bridge()'s frames are the simulator's truth.
// Qt's virtualcan plugin makes the bus virtual: interfaces can0…can9 on a TCP server on 127.0.0.1 (one per process,
// started by the first device that opens; port 35468, or "tcp://127.0.0.1:<port>/canN"); a device writes FD frames only
// with CanFdKey set; frames are not echoed to their sender.
#pragma once

#include "BridgeTransport.h"

#include <QCanBusDevice>

class BridgeCanGateway : public QObject
{
    Q_OBJECT
public:
    struct Stats {
        quint64 toBus = 0, toFirmware = 0, dropped = 0;
        quint64 refused = 0; // can_rx the bridge did not accept
    };

    // Takes ownership of device (CAN FD enabled: CanTransport::createDevice). The bridge runs in real time whatever
    // bridge.timeFactor says: the VCU on the bus sends on the wall clock and the firmware judges the age of its frames
    // (FW-11, 20 ms).
    BridgeCanGateway(BridgeTransport::Options bridge, QCanBusDevice *device, QObject *parent = nullptr);
    ~BridgeCanGateway() override;

    void start(); // connects the device, then starts the bridge
    BridgeTransport &bridge() { return m_bridge; }
    const Stats &stats() const { return m_stats; }

Q_SIGNALS:
    void failed(const QString &why);

private:
    void toBus(const QString &direction, const QString &line);
    void toFirmware();

    BridgeTransport m_bridge;
    QCanBusDevice *m_dev;
    Stats m_stats;
};
