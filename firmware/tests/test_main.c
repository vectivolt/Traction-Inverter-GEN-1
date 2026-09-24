/* test_main.c — runs every suite; exit code = number of failed checks (capped). */
#include <string.h>

#include "app.h"
#include "dtc.h"
#include "fault_mgr.h"
#include "sim.h"
#include "test.h"

unsigned t_checks;
unsigned t_fails;
const char *t_current = "";
static unsigned s_tests;

void t_run(const char *name, void (*fn)(void))
{
    t_current = name;
    s_tests++;
    sim_fs26_config(NULL);
    sim_reset();
    sim_nvm_wipe();
    dtc_init();
    (void)memset(&g_fm_retained, 0, sizeof g_fm_retained); /* cold start: retained RAM lost */
    (void)memset(&g_app_session, 0, sizeof g_app_session);
    const unsigned before = t_fails;
    fn();
    if (t_fails != before) {
        printf("  -> %s failed\n", name);
    }
}

int main(void)
{
    void (*const suites[])(void) = {
        suite_crc,         suite_params,     suite_board_map,  suite_platform_cfg,  suite_current,   suite_vdc,
        suite_temp,        suite_hvil,       suite_hwid,       suite_ign,       suite_resolver,
        suite_foc,         suite_torque,     suite_dclink,     suite_gains,     suite_can_cmd,
        suite_dtc,         suite_discharge,  suite_nvlog,      suite_calib,     suite_safe_state,
        suite_fault_mgr,   suite_fs26,       suite_bridge,     suite_gate_power, suite_gate_selftest,
        suite_state_machine, suite_scenarios,
    };
    for (unsigned i = 0u; i < sizeof suites / sizeof suites[0]; i++) {
        suites[i]();
    }
    printf("\n%u tests, %u checks, %u failed\n", s_tests, t_checks, t_fails);
    return (t_fails > 0u) ? 1 : 0;
}
