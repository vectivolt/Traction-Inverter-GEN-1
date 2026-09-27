// DtcTracker — the DTC picture over time: the latest list and a history of raise/clear/occurrence events.
// The simulator reports every DTC with an occurrence (a complete list); CAN reports a summary (the confirmed count
// and the first active DTC, INV_STATUS b11-13), which the tracker follows as far as it goes.
#pragma once

#include "Telemetry.h"

#include <QHash>
#include <QVector>

struct DtcEvent {
    enum class Kind { Raised, Cleared, Occurrence };
    double tMs = 0;  // source time of the frame that showed it
    qint64 rxMs = 0; // host time
    int id = 0;
    Kind kind = Kind::Raised;
    int occ = 0;
};

class DtcTracker
{
public:
    // Returns the events this frame produced (also appended to history()).
    QVector<DtcEvent> update(const TelemetryFrame &f);
    void clear();
    const QVector<DtcState> &current() const { return m_current; }
    QVector<DtcState> active() const;
    const QVector<DtcEvent> &history() const { return m_history; }
    bool complete() const { return m_complete; }
    int confirmedCount() const { return m_confirmed; } // INV_STATUS b11 on CAN, counted on the simulator

private:
    struct Seen {
        bool active = false;
        int occ = 0;
    };
    QHash<int, Seen> m_seen;
    QVector<DtcState> m_current;
    QVector<DtcEvent> m_history;
    bool m_complete = false;
    int m_confirmed = 0;
};
