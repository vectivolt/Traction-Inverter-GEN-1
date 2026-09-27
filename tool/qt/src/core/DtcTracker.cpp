#include "DtcTracker.h"

namespace {
constexpr int HISTORY_MAX = 5000;
}

QVector<DtcEvent> DtcTracker::update(const TelemetryFrame &f)
{
    QVector<DtcEvent> ev;
    auto push = [&](int id, DtcEvent::Kind k, int occ) {
        DtcEvent e;
        e.tMs = f.tMs;
        e.rxMs = f.rxMs;
        e.id = id;
        e.kind = k;
        e.occ = occ;
        ev.push_back(e);
    };
    m_complete = f.dtcListComplete;
    const bool haveSummary = f.has(QStringLiteral("inv.n_dtc"));
    if (!f.dtcListComplete && !haveSummary) {
        return ev; // this frame says nothing about DTCs
    }
    QHash<int, Seen> now;
    for (const DtcState &d : f.dtcs) {
        now.insert(d.id, {d.active(), d.occ});
    }
    for (auto it = now.begin(); it != now.end(); ++it) {
        const Seen before = m_seen.value(it.key());
        if (it->active && !before.active) {
            push(it.key(), DtcEvent::Kind::Raised, it->occ);
        } else if (it->active && before.active && it->occ > before.occ) {
            push(it.key(), DtcEvent::Kind::Occurrence, it->occ); // failed again between two frames
        } else if (!it->active && before.active) {
            push(it.key(), DtcEvent::Kind::Cleared, it->occ);
        }
    }
    for (auto it = m_seen.begin(); it != m_seen.end(); ++it) {
        if (it->active && !now.contains(it.key())) {
            push(it.key(), DtcEvent::Kind::Cleared, it->occ); // gone from the list (cleared, or no longer first on CAN)
        }
    }
    m_seen = now;
    m_current = f.dtcs;
    if (f.dtcListComplete) {
        m_confirmed = 0;
        for (const DtcState &d : f.dtcs) {
            m_confirmed += d.confirmed() ? 1 : 0;
        }
    } else {
        m_confirmed = static_cast<int>(f.v(QStringLiteral("inv.n_dtc"), 0.0));
    }
    m_history += ev;
    if (m_history.size() > HISTORY_MAX) {
        m_history.remove(0, m_history.size() - HISTORY_MAX);
    }
    return ev;
}

void DtcTracker::clear()
{
    m_seen.clear();
    m_current.clear();
    m_history.clear();
    m_confirmed = 0;
}

QVector<DtcState> DtcTracker::active() const
{
    QVector<DtcState> a;
    for (const DtcState &d : m_current) {
        if (d.active()) {
            a.push_back(d);
        }
    }
    return a;
}
