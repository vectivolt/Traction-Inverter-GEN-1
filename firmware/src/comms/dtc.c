/* dtc.c — the DTC store. */
#include "dtc.h"

static dtc_entry_t s_e[DTC_COUNT];
static bool s_dirty;

void dtc_init(void)
{
    for (uint32_t i = 0u; i < (uint32_t)DTC_COUNT; i++) {
        s_e[i] = (dtc_entry_t){.status = (uint8_t)(DTC_TNCSLC | DTC_TNCTOC)};
    }
    s_dirty = false;
}

static bool ok_id(dtc_id_t id) { return (id > DTC_NONE) && (id < DTC_COUNT); }

void dtc_set(dtc_id_t id, uint32_t now_ms)
{
    if (!ok_id(id)) {
        return;
    }
    dtc_entry_t *e = &s_e[id];
    if ((e->status & DTC_TF) == 0u) {
        e->occ = (e->occ < 255u) ? (uint8_t)(e->occ + 1u) : 255u;
        s_dirty = true;
    }
    if (e->first_ms == 0u) {
        e->first_ms = now_ms;
    }
    e->last_ms = now_ms;
    e->status |= (uint8_t)(DTC_TF | DTC_TFTOC | DTC_PDTC | DTC_CDTC | DTC_TFSLC);
    e->status &= (uint8_t)~(uint8_t)(DTC_TNCSLC | DTC_TNCTOC);
}

void dtc_pass(dtc_id_t id)
{
    if (ok_id(id)) {
        s_e[id].status &= (uint8_t)~(uint8_t)(DTC_TF | DTC_TNCSLC | DTC_TNCTOC);
    }
}

bool dtc_active(dtc_id_t id) { return ok_id(id) && ((s_e[id].status & DTC_TF) != 0u); }
uint8_t dtc_status(dtc_id_t id) { return ok_id(id) ? s_e[id].status : 0u; }
uint8_t dtc_occurrences(dtc_id_t id) { return ok_id(id) ? s_e[id].occ : 0u; }
uint32_t dtc_code(dtc_id_t id) { return 0xD10000u | (uint32_t)id; }

uint16_t dtc_confirmed_count(void)
{
    uint16_t n = 0u;
    for (uint32_t i = 1u; i < (uint32_t)DTC_COUNT; i++) {
        n += ((s_e[i].status & DTC_CDTC) != 0u) ? 1u : 0u;
    }
    return n;
}

dtc_id_t dtc_first_active(void)
{
    for (uint32_t i = 1u; i < (uint32_t)DTC_COUNT; i++) {
        if ((s_e[i].status & DTC_TF) != 0u) {
            return (dtc_id_t)i;
        }
    }
    return DTC_NONE;
}

void dtc_clear_all(void)
{
    dtc_init();
    s_dirty = true;
}

void dtc_new_cycle(void)
{
    for (uint32_t i = 1u; i < (uint32_t)DTC_COUNT; i++) {
        s_e[i].status &= (uint8_t)~(uint8_t)DTC_TFTOC;
        s_e[i].status |= DTC_TNCTOC;
    }
}

bool dtc_take_dirty(void)
{
    const bool d = s_dirty;
    s_dirty = false;
    return d;
}
