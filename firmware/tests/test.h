/* test.h — minimal assert framework: every CHECK counts, failures print file:line and the test,
 * the runner exits non-zero if anything failed. Each test starts from a fresh simulation. */
#ifndef TEST_H
#define TEST_H

#include <math.h>
#include <stdio.h>

extern unsigned t_checks;
extern unsigned t_fails;
extern const char *t_current;

#define CHECK(c)                                                                                  \
    do {                                                                                          \
        t_checks++;                                                                               \
        if (!(c)) {                                                                               \
            t_fails++;                                                                            \
            printf("  FAIL %s:%d [%s]: %s\n", __FILE__, __LINE__, t_current, #c);                 \
        }                                                                                         \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                     \
    do {                                                                                          \
        t_checks++;                                                                               \
        const double a_ = (double)(a);                                                            \
        const double b_ = (double)(b);                                                            \
        if (!(fabs(a_ - b_) <= (double)(tol))) {                                                  \
            t_fails++;                                                                            \
            printf("  FAIL %s:%d [%s]: %s = %.6g, expected %.6g +/- %.3g\n", __FILE__, __LINE__, \
                   t_current, #a, a_, b_, (double)(tol));                                         \
        }                                                                                         \
    } while (0)

#define TEST(name) static void name(void)
#define RUN(name) t_run(#name, name)

void t_run(const char *name, void (*fn)(void));

/* suites (one per test file) */
void suite_crc(void);
void suite_time(void);
void suite_params(void);
void suite_board_map(void);
void suite_platform_cfg(void);
void suite_current(void);
void suite_vdc(void);
void suite_temp(void);
void suite_hvil(void);
void suite_hwid(void);
void suite_ign(void);
void suite_resolver(void);
void suite_foc(void);
void suite_torque(void);
void suite_dclink(void);
void suite_gains(void);
void suite_can_cmd(void);
void suite_dtc(void);
void suite_discharge(void);
void suite_nvlog(void);
void suite_calib(void);
void suite_safe_state(void);
void suite_fault_mgr(void);
void suite_fs26(void);
void suite_bridge(void);
void suite_gate_power(void);
void suite_gate_selftest(void);
void suite_state_machine(void);
void suite_scenarios(void);

#endif /* TEST_H */
