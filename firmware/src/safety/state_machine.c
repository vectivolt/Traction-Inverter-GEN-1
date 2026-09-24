/* state_machine.c — transitions (docs/state-machine.md has the diagram). */
#include "state_machine.h"

#include "ti_math.h"

#define TORQUE_ACTIVE_NM 0.5f

void sm_init(sm_t *s)
{
    *s = (sm_t){0};
    s->st = SM_OFF;
}

static void go(sm_t *s, sm_state_t st, uint32_t now_ms)
{
    if (st == SM_DISCHARGE) {
        s->before_discharge = s->st;
    }
    if ((st == SM_INIT) || (st == SM_OFF)) {
        s->step4_done = false;
        s->step5_done = false;
        s->step6_done = false;
        s->self_test_done = false;
        s->init_clear_done = false;
        s->retry_mode = false;
    }
    s->st = st;
    s->t_enter_ms = now_ms;
}

static bool dwell(const sm_t *s, const sm_in_t *in, uint32_t ms) { return ti_elapsed(in->now_ms, s->t_enter_ms, ms); }

/* Exits every armed/non-armed operating state shares. Returns true if it moved. */
static bool common_exits(sm_t *s, const sm_in_t *in)
{
    if (!in->ign_on || in->shutdown_req) {
        go(s, SM_SAFE_POWERDOWN, in->now_ms);
        return true;
    }
    if (in->fault_needed) {
        go(s, SM_FAULT, in->now_ms);
        return true;
    }
    if (in->discharge_req && (in->contactors == TI_CONT_OPEN) && !s->dis_req_seen) {
        s->dis_req_seen = true;
        go(s, SM_DISCHARGE, in->now_ms);
        return true;
    }
    return false;
}

static void st_sensor_selftest(sm_t *s, const sm_in_t *in, sm_out_t *o, const ti_params_t *p)
{
    if (!in->v5gd_ok || in->flt_low_at_boot || in->rdy_before_gate_power || in->gate_power_failed) {
        go(s, SM_FAULT, in->now_ms);
        return;
    }
    if (!in->sensors_ok) {
        if (dwell(s, in, p->cal_sensor_selftest_ms)) {
            go(s, SM_FAULT, in->now_ms);
        }
        return;
    }
    if (!in->fs0b_released) {
        o->req_fs0b_release = true;
        if (dwell(s, in, p->cal_sensor_selftest_ms + p->cal_fs0b_release_ms)) {
            go(s, SM_FAULT, in->now_ms);
        }
        return;
    }
    s->step4_done = true;
    if (!s->step5_done) {
        o->req_asc_decision = true;
        s->step5_done = true;
    }
    o->req_gate_power = true;
    s->step6_done = true;
    if (in->gate_power_ready) {
        go(s, SM_VEHICLE_HANDSHAKE, in->now_ms);
    }
}

static void st_fault(sm_t *s, const sm_in_t *in, sm_out_t *o)
{
    o->self_test_done = s->self_test_done;
    if (!in->ign_on || in->shutdown_req) {
        go(s, SM_SAFE_POWERDOWN, in->now_ms);
        return;
    }
    if (in->discharge_req && (in->contactors == TI_CONT_OPEN) && !s->dis_req_seen) {
        s->dis_req_seen = true;
        go(s, SM_DISCHARGE, in->now_ms);
        return;
    }
    if (in->retry_allowed) {
        o->req_recovery = true;
    }
    if (in->recovery_done) {
        s->retry_mode = true;
        go(s, SM_VEHICLE_HANDSHAKE, in->now_ms);
        return;
    }
    if (!in->fault_needed && s->step6_done) {
        go(s, SM_VEHICLE_HANDSHAKE, in->now_ms); /* non-latched rows cleared, or the VCU reset them */
    }
}

static void st_run(sm_t *s, const sm_in_t *in, sm_out_t *o)
{
    o->arm = true;
    o->self_test_done = true;
    o->torque_enable = true;
    o->torque_reduced = s->retry_mode;
    if (common_exits(s, in)) {
        return;
    }
    if (in->contactors != TI_CONT_CLOSED) {
        go(s, SM_ARMED_ZERO_TORQUE, in->now_ms);
        return;
    }
    if ((!in->cmd_fresh || !in->enable_req) && in->torque_ramped_out) {
        go(s, SM_ARMED_ZERO_TORQUE, in->now_ms); /* FW-11: ramped to zero, not held */
        return;
    }
    if ((s->st == SM_RUN) && in->derate_active) {
        go(s, SM_DERATE, in->now_ms);
    } else if ((s->st == SM_DERATE) && !in->derate_active) {
        go(s, SM_RUN, in->now_ms);
    } else {
        /* stay */
    }
}

void sm_step(sm_t *s, const sm_in_t *in, sm_out_t *o, const ti_params_t *p)
{
    *o = (sm_out_t){0};
    if (!in->discharge_req) {
        s->dis_req_seen = false;
    }
    switch (s->st) {
    case SM_OFF:
        if (in->ign_on) {
            go(s, SM_INIT, in->now_ms);
        }
        break;
    case SM_INIT:
        if (!in->ign_on) {
            go(s, SM_SAFE_POWERDOWN, in->now_ms);
        } else if (in->init == SM_FAIL) {
            go(s, SM_FAULT, in->now_ms);
        } else if (in->init == SM_OK) {
            if (!s->init_clear_done) {
                o->req_flt_clear = true; /* §9 step 2: through the one-shot (FS0B still holds DRV_EN) */
                s->init_clear_done = true;
            } else {
                go(s, SM_SENSOR_SELFTEST, in->now_ms);
            }
        } else {
            /* FS26 readback / identity / calibration in progress */
        }
        break;
    case SM_SENSOR_SELFTEST:
        if (!in->ign_on) {
            go(s, SM_SAFE_POWERDOWN, in->now_ms);
        } else {
            st_sensor_selftest(s, in, o, p);
        }
        break;
    case SM_VEHICLE_HANDSHAKE:
        o->self_test_done = s->self_test_done;
        if (!common_exits(s, in) && in->cmd_fresh) {
            go(s, s->self_test_done ? SM_PRECHARGE_WAIT : SM_GATE_SELFTEST, in->now_ms);
        }
        break;
    case SM_GATE_SELFTEST:
        o->req_selftest = true;
        if (!in->ign_on || in->shutdown_req) {
            go(s, SM_SAFE_POWERDOWN, in->now_ms);
        } else if (in->selftest == SM_FAIL) {
            go(s, SM_FAULT, in->now_ms);
        } else if (in->selftest == SM_OK) {
            s->self_test_done = true;
            o->self_test_done = true;
            go(s, in->fault_needed ? SM_FAULT : SM_PRECHARGE_WAIT, in->now_ms);
        } else {
            /* running */
        }
        break;
    case SM_PRECHARGE_WAIT:
        o->self_test_done = true;
        if (common_exits(s, in)) {
            break;
        }
        if (in->precharge == SM_FAIL) {
            go(s, SM_FAULT, in->now_ms);
        } else if ((in->contactors == TI_CONT_CLOSED) && in->link_at_pack && (in->precharge != SM_FAIL)) {
            go(s, SM_ARMED_ZERO_TORQUE, in->now_ms);
        } else {
            /* the VCU precharges */
        }
        break;
    case SM_ARMED_ZERO_TORQUE:
        o->arm = true;
        o->self_test_done = true;
        o->torque_reduced = s->retry_mode;
        if (common_exits(s, in)) {
            break;
        }
        if ((in->contactors != TI_CONT_CLOSED) && (ti_absf(in->speed_rpm) < in->n_x_rpm)) {
            go(s, SM_PRECHARGE_WAIT, in->now_ms); /* normal opening at zero torque */
        } else if (in->cmd_fresh && in->enable_req && (ti_absf(in->torque_req_nm) > TORQUE_ACTIVE_NM) &&
                   (in->contactors == TI_CONT_CLOSED)) {
            go(s, SM_RUN, in->now_ms);
        } else {
            /* armed */
        }
        break;
    case SM_RUN:
    case SM_DERATE:
        st_run(s, in, o);
        break;
    case SM_FAULT:
        st_fault(s, in, o);
        break;
    case SM_DISCHARGE:
        o->req_discharge = true;
        if (!in->ign_on && in->discharge_done) {
            go(s, SM_SAFE_POWERDOWN, in->now_ms);
        } else if (in->discharge_done || (in->contactors != TI_CONT_OPEN)) {
            go(s, (s->before_discharge == SM_FAULT) ? SM_FAULT : SM_VEHICLE_HANDSHAKE, in->now_ms);
        } else {
            /* discharging */
        }
        break;
    case SM_SAFE_POWERDOWN:
        o->req_discharge = (in->contactors == TI_CONT_OPEN) && !in->discharge_done;
        if (in->ign_on && !in->shutdown_req) {
            go(s, SM_INIT, in->now_ms);
        } else if ((in->discharge_done || (in->contactors != TI_CONT_OPEN)) && in->nvm_idle) {
            o->req_lpoff = true;
            go(s, SM_OFF, in->now_ms);
        } else {
            /* discharge / NVM flush in progress */
        }
        break;
    default:
        go(s, SM_FAULT, in->now_ms);
        break;
    }
}

const char *sm_name(sm_state_t st)
{
    static const char *const N[SM_STATE_COUNT] = {"OFF", "INIT", "SENSOR_SELFTEST", "VEHICLE_HANDSHAKE", "PRECHARGE_WAIT",
                                                  "GATE_SELFTEST", "ARMED_ZERO_TORQUE", "RUN", "DERATE", "FAULT",
                                                  "DISCHARGE", "SAFE_POWERDOWN"};
    return (st < SM_STATE_COUNT) ? N[st] : "?";
}
