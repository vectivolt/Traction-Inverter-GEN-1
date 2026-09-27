// DashboardPage — the inverter at a glance: the §9 state track, arming, link voltage and speed gauges, torque
// command vs APPLIED torque, dq and phase currents, power, temperatures against their derating band, the limits in
// force, the watchdog/heartbeat picture and the §6 safety decision; under the header the image verifier's root of trust
// (Page::addRootBanner). Everything greys out and shows its age the moment the data stop being live.
#pragma once

#include "Page.h"

class ArcGauge;
class BandBar;
class KvGrid;
class QLabel;
class StateTrack;
class Tile;

class DashboardPage : public Page
{
    Q_OBJECT
public:
    explicit DashboardPage(Session &s, QWidget *parent = nullptr);

private:
    void onFrame(const TelemetryFrame &f);
    void onTick();
    double limit(const char *name, double def) const;

    StateTrack *m_track;
    QLabel *m_stateName, *m_armPill, *m_hvPill, *m_bridgePill, *m_armDetail;
    ArcGauge *m_speed, *m_vdc;
    Tile *m_torque, *m_power, *m_dq, *m_iph;
    QVector<BandBar *> m_temps;
    KvGrid *m_limits, *m_wd, *m_safety;
    bool m_haveLimits = false;
};
