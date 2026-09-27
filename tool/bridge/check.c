/* check.c — `make check`: the bridge's stdio self-test. It starts sim_bridge as a child process (flat-out, no
 * wall-clock pacing), speaks only the documented protocol (tool/PROTOCOL.md) and fails on the first expectation
 * that does not hold within its simulated-time budget:
 *   hello (firmware ID, DTC names, the bench key provisioned) -> the firmware boots to PRECHARGE_WAIT (section 9:
 *   FW-12/01/02/20, sensor self-test, FS0B release, gate power, FW-16) -> "arm": precharge, contactors,
 *   ARMED_ZERO_TORQUE with MCU_GATE_EN -> round 23: FW-39 service mode from the tool (paused and stepped: the tool's
 *   heartbeat is exact) — SecurityAccess with the bench key, a start refused while the bench VCU reports 5 km/h, the
 *   Rs routine on the locked rotor (attestation LK) run to its end under the heartbeat, Rs against the plant ->
 *   dyno to 1500 rpm -> 120 Nm: RUN, the current loop closed on the plant (firmware and plant torque) -> round 23
 *   (item 2): 7500 rpm at 750 V with zero torque — the bridge modulates, no uncommanded regen -> a parameter write
 *   the firmware's ti_params_validate() accepts and one it refuses -> a high-side DESAT: DTC_DESAT_HS active, FAULT ->
 *   the FW-15 retry, a second DESAT: DTC_DESAT_REPEAT -> round 23: 0x19 04 both DESAT snapshots and 0x19 0A over the
 *   segmented transport (the tester's flow control), the 0x2A periodic stream on 0x6E9 started and stopped,
 *   dtc_clear refused with HV present (the image's 0x14 gate) -> "disarm": contactors open, gate enable low ->
 *   discharge, dtc_clear through 0x14 (the DESAT latches kept) -> the bench key withdrawn: 27 01 answers NRC 0x22
 *   (fail closed) -> stdin closed: the bridge exits 0. */
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static FILE *s_to;    /* the bridge's stdin */
static FILE *s_from;  /* the bridge's stdout */
static char s_line[1 << 17];
static char s_hello[1 << 17];
static double s_t;    /* simulated time of the last telemetry frame, ms */
static int s_fails;

/* ---- a minimal JSON path lookup (objects by key, dotted path) ---- */
static const char *skip_ws(const char *p)
{
    while ((*p == ' ') || (*p == '\n') || (*p == '\t') || (*p == '\r')) {
        p++;
    }
    return p;
}

static const char *skip_value(const char *p)
{
    p = skip_ws(p);
    if (*p == '"') {
        for (p++; (*p != '\0') && (*p != '"'); p++) {
            if ((*p == '\\') && (p[1] != '\0')) {
                p++;
            }
        }
        return (*p == '"') ? (p + 1) : p;
    }
    if ((*p == '{') || (*p == '[')) {
        int depth = 0;
        bool in_str = false;
        for (; *p != '\0'; p++) {
            if (in_str) {
                if ((*p == '\\') && (p[1] != '\0')) {
                    p++;
                } else if (*p == '"') {
                    in_str = false;
                }
            } else if (*p == '"') {
                in_str = true;
            } else if ((*p == '{') || (*p == '[')) {
                depth++;
            } else if ((*p == '}') || (*p == ']')) {
                if (--depth == 0) {
                    return p + 1;
                }
            }
        }
        return p;
    }
    while ((*p != '\0') && (*p != ',') && (*p != '}') && (*p != ']')) {
        p++;
    }
    return p;
}

/* the value of "a.b.c" in a JSON object, or NULL */
static const char *jpath(const char *json, const char *path)
{
    const char *p = skip_ws(json);
    char seg[64];
    while (*path != '\0') {
        size_t n = strcspn(path, ".");
        if (n >= sizeof seg) {
            return NULL;
        }
        memcpy(seg, path, n);
        seg[n] = '\0';
        path += n + ((path[n] == '.') ? 1u : 0u);
        if (*p != '{') {
            return NULL;
        }
        p = skip_ws(p + 1);
        bool found = false;
        while ((*p == '"') && !found) {
            const char *k = p + 1;
            const char *e = strchr(k, '"');
            if (e == NULL) {
                return NULL;
            }
            p = skip_ws(e + 1);
            if (*p != ':') {
                return NULL;
            }
            p = skip_ws(p + 1);
            if (((size_t)(e - k) == strlen(seg)) && (strncmp(k, seg, strlen(seg)) == 0)) {
                found = true;
            } else {
                p = skip_ws(skip_value(p));
                if (*p == ',') {
                    p = skip_ws(p + 1);
                }
            }
        }
        if (!found) {
            return NULL;
        }
    }
    return p;
}

static double jnum(const char *json, const char *path)
{
    const char *v = jpath(json, path);
    return (v == NULL) ? -1.0e300 : strtod(v, NULL);
}

static bool jtrue(const char *json, const char *path)
{
    const char *v = jpath(json, path);
    return (v != NULL) && (strncmp(v, "true", 4) == 0);
}

static bool jstr_is(const char *json, const char *path, const char *want)
{
    const char *v = jpath(json, path);
    return (v != NULL) && (*v == '"') && (strncmp(v + 1, want, strlen(want)) == 0) && (v[1 + strlen(want)] == '"');
}

/* the DTC id of a name, from hello.dtcs (its index) */
static int dtc_id(const char *name)
{
    const char *p = jpath(s_hello, "dtcs");
    if ((p == NULL) || (*p != '[')) {
        return -1;
    }
    p = skip_ws(p + 1);
    for (int i = 0; *p == '"'; i++) {
        if ((strncmp(p + 1, name, strlen(name)) == 0) && (p[1 + strlen(name)] == '"')) {
            return i;
        }
        p = skip_ws(skip_value(p));
        p = (*p == ',') ? skip_ws(p + 1) : p;
    }
    return -1;
}

/* testFailed status bit of a DTC in the telemetry's dtc array [[id, status, occ, first_ms, last_ms], ...] */
static bool dtc_active(const char *tel, int id)
{
    const char *p = jpath(tel, "dtc");
    if ((p == NULL) || (*p != '[')) {
        return false;
    }
    for (p = skip_ws(p + 1); *p == '['; p = skip_ws(p)) {
        char *e = NULL;
        const long i = strtol(p + 1, &e, 10);
        const long st = strtol(e + 1, NULL, 10);
        if ((i == id) && ((st & 1) != 0)) {
            return true;
        }
        p = skip_ws(skip_value(p));
        p = (*p == ',') ? (p + 1) : p;
    }
    return false;
}

/* ---- the conversation ---- */
static bool next_line(void)
{
    if (fgets(s_line, sizeof s_line, s_from) == NULL) {
        return false;
    }
    if (jstr_is(s_line, "type", "tel")) {
        s_t = jnum(s_line, "t_ms");
    }
    if (jstr_is(s_line, "type", "hello")) {
        (void)snprintf(s_hello, sizeof s_hello, "%s", s_line); /* a power cycle prints the current values */
    }
    return true;
}

static void send(const char *cmd)
{
    (void)fprintf(s_to, "%s\n", cmd);
    (void)fflush(s_to);
}

typedef bool (*pred_fn)(const char *tel, const void *arg);

/* reads until a telemetry frame satisfies pred, or `budget_ms` of simulated time passed */
static bool wait_tel(const char *what, pred_fn pred, const void *arg, double budget_ms)
{
    const double t_end = s_t + budget_ms;
    while (next_line()) {
        if (!jstr_is(s_line, "type", "tel")) {
            continue;
        }
        if (pred(s_line, arg)) {
            (void)printf("  ok    %-58s t = %8.1f ms\n", what, s_t);
            return true;
        }
        if (s_t > t_end) {
            break;
        }
    }
    (void)printf("  FAIL  %-58s (budget %.0f ms of simulated time)\n        last frame: %.300s\n", what, budget_ms,
                 s_line);
    s_fails++;
    return false;
}

/* reads until the ack of request `id` arrives (telemetry in between is skipped); returns its line */
static const char *wait_ack(int id)
{
    while (next_line()) {
        if (jstr_is(s_line, "type", "ack") && (jnum(s_line, "id") == (double)id)) {
            return s_line;
        }
    }
    return NULL;
}

static void expect(bool ok, const char *what)
{
    (void)printf("  %-5s %s\n", ok ? "ok" : "FAIL", what);
    s_fails += ok ? 0 : 1;
}

/* ---- round 23: the tester (uds acks carry "msg", the UDS payload the bridge reassembled, and "frames") ---- */
static int s_id = 100;

static const char *cmd(const char *json_without_id) /* {"cmd":...} without the closing brace; returns its ack */
{
    char c[512];
    const int id = s_id++;
    (void)snprintf(c, sizeof c, "%s,\"id\":%d}", json_without_id, id);
    send(c);
    return wait_ack(id);
}

static const char *uds(const char *hex)
{
    char c[512];
    (void)snprintf(c, sizeof c, "{\"cmd\":\"uds\",\"hex\":\"%s\"", hex);
    return cmd(c);
}

static bool msg_is(const char *ack, const char *want)
{
    const char *v = (ack == NULL) ? NULL : jpath(ack, "msg");
    return (v != NULL) && (*v == '"') && (strncmp(v + 1, want, strlen(want)) == 0);
}

static int msg_len(const char *ack)
{
    const char *v = (ack == NULL) ? NULL : jpath(ack, "msg");
    if ((v == NULL) || (*v != '"') || (v[1] == '"')) {
        return 0;
    }
    const char *e = strchr(v + 1, '"');
    return (e == NULL) ? 0 : (int)(((e - (v + 1)) + 1) / 3);
}

static int ack_frames(const char *ack) { return (jpath(ack, "frames") != NULL) ? (int)jnum(ack, "frames") : 0; }

static int msg_byte(const char *ack, int i)
{
    const char *v = (ack == NULL) ? NULL : jpath(ack, "msg");
    if ((v == NULL) || (*v != '"') || (i >= msg_len(ack))) {
        return -1;
    }
    return (int)strtol(v + 1 + (3 * i), NULL, 16);
}

static float msg_f32(const char *ack, int i) /* big-endian IEEE float, as the snapshot record stores it */
{
    unsigned u = 0u;
    for (int k = 0; k < 4; k++) {
        u = (u << 8) | (unsigned)msg_byte(ack, i + k);
    }
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

/* SecurityAccess with the bench key: key[i] = seed[(i + 1) % 4] ^ (0xA5 + i); a zero seed = already unlocked */
static bool unlock(void)
{
    const char *a = uds("02 27 01");
    if (!msg_is(a, "67 01")) {
        return false;
    }
    int s[4];
    for (int i = 0; i < 4; i++) {
        s[i] = msg_byte(a, 2 + i);
    }
    if ((s[0] | s[1] | s[2] | s[3]) == 0) {
        return true;
    }
    char h[64];
    (void)snprintf(h, sizeof h, "06 27 02 %02X %02X %02X %02X", s[1] ^ 0xA5, s[2] ^ 0xA6, s[3] ^ 0xA7, s[0] ^ 0xA8);
    return msg_is(uds(h), "67 02");
}

/* reads until telemetry passes session time t_end; counts the periodic lines of pDID `pdid` on the way */
static int periodic_until(double t_end, int pdid)
{
    int n = 0;
    while (next_line()) {
        if (jstr_is(s_line, "type", "periodic") && (jnum(s_line, "pdid") == (double)pdid) &&
            (jnum(s_line, "did") == (double)(0xF200 | pdid))) {
            n++;
        }
        if (jstr_is(s_line, "type", "tel") && (s_t >= t_end)) {
            break;
        }
    }
    return n;
}

static bool p_sm(const char *t, const void *a) { return jnum(t, "state.sm") == (double)*(const int *)a; }
static bool p_armed(const char *t, const void *a)
{
    (void)a;
    return (jnum(t, "state.sm") == 6.0) && jtrue(t, "state.gate_en") && jtrue(t, "state.drv_en");
}
static bool p_speed(const char *t, const void *a) { return jnum(t, "motion.speed_rpm") >= *(const double *)a; }
static bool p_torque(const char *t, const void *a)
{
    const double want = *(const double *)a;
    return (jnum(t, "state.sm") == 7.0) && (jnum(t, "state.bridge") == 2.0) &&
           (jnum(t, "motion.torque_est_nm") > (0.9 * want)) && (jnum(t, "plant.torque_nm") > (0.9 * want));
}
static bool p_dtc(const char *t, const void *a) { return dtc_active(t, *(const int *)a) && (jnum(t, "state.sm") == 9.0); }
static bool p_disarmed(const char *t, const void *a)
{
    (void)a;
    return jstr_is(t, "vcu.seq", "idle") && (jnum(t, "link.contactors") == 1.0) && !jtrue(t, "state.gate_en");
}
static bool p_speed_le(const char *t, const void *a) { return jnum(t, "motion.speed_rpm") <= *(const double *)a; }
static bool p_after(const char *t, const void *a) { return jnum(t, "t_ms") >= *(const double *)a; }
static bool p_run(const char *t, const void *a)
{
    (void)a;
    return (jnum(t, "state.sm") == 7.0) && (jnum(t, "state.bridge") == 2.0);
}
/* round 23 (item 2): above the speed where the back-EMF exceeds the link, zero torque is modulated (field current) —
 * the pack neither charges through the diodes nor feeds more than the losses */
static bool p_emf(const char *t, const void *a)
{
    (void)a;
    const double idc = jnum(t, "plant.i_dc_a");
    return (jnum(t, "plant.speed_rpm") >= 7450.0) && (jnum(t, "state.bridge") == 2.0) && (idc > -5.0) && (idc < 5.0) &&
           (jnum(t, "plant.torque_nm") > -5.0) && (jnum(t, "plant.torque_nm") < 5.0);
}
static bool p_hv_safe(const char *t, const void *a)
{
    (void)a;
    return jnum(t, "state.hv") == 1.0;
}
static bool p_blocked(const char *t, const void *a) /* FAULT, the DTC named */
{
    return (jnum(t, "state.sm") == 9.0) && dtc_active(t, *(const int *)a) && !jtrue(t, "state.gate_en");
}

int main(int argc, char **argv)
{
    const char *exe = (argc > 1) ? argv[1] : "./sim_bridge";
    int to[2];
    int from[2];
    if ((pipe(to) != 0) || (pipe(from) != 0)) {
        perror("pipe");
        return 2;
    }
    const pid_t pid = fork();
    if (pid == 0) {
        (void)dup2(to[0], 0);
        (void)dup2(from[1], 1);
        (void)close(to[1]);
        (void)close(from[0]);
        execl(exe, exe, "--time", "0", "--rate", "50", (char *)NULL);
        perror("exec sim_bridge");
        _exit(127);
    }
    (void)close(to[0]);
    (void)close(from[1]);
    s_to = fdopen(to[1], "w");
    s_from = fdopen(from[0], "r");
    (void)signal(SIGPIPE, SIG_IGN);
    (void)printf("sim_bridge self-test (%s)\n", exe);

    bool have_hello = false;
    while (!have_hello && next_line()) {
        have_hello = jstr_is(s_line, "type", "hello"); /* next_line() keeps it in s_hello */
    }
    const int desat = dtc_id("DTC_DESAT_HS");
    const int repeat = dtc_id("DTC_DESAT_REPEAT");
    const int n_dtcs = (int)jnum(s_hello, "dtc_count") - 1;
    const char *fw = jpath(s_hello, "fw_id");
    expect(have_hello && (fw != NULL) && (desat > 0) && (repeat > 0), "hello: firmware ID and the DTC name table");
    if (!have_hello) {
        return 1;
    }
    (void)printf("        firmware %.10s, %d DTCs (DTC_DESAT_HS = %d)\n", fw + 1, (int)jnum(s_hello, "dtc_count"), desat);
    expect(jtrue(s_hello, "provision.sa_key"), "hello: the bench SecurityAccess key provisioned (provision.sa_key)");

    const int precharge_wait = 4;
    wait_tel("boot to PRECHARGE_WAIT (section 9 steps 1-7)", p_sm, &precharge_wait, 5000.0);
    send("{\"cmd\":\"arm\",\"id\":1}");
    wait_tel("arm: precharge, contactors, ARMED_ZERO_TORQUE, DRV_EN", p_armed, NULL, 5000.0);

    /* round 23: FW-39 service mode from the tool — the VCU's enable withdrawn (no torque request in service mode),
     * the dyno holding the rotor at 0 rpm, the bench VCU's vehicle speed valid and zero; paused and stepped */
    (void)cmd("{\"cmd\":\"enable\",\"on\":false");
    (void)cmd("{\"cmd\":\"pause\",\"on\":true");
    (void)cmd("{\"cmd\":\"step\",\"ms\":20");
    expect(unlock(), "UDS SecurityAccess with the bench key: 27 01 seed, 27 02 key accepted");
    (void)cmd("{\"cmd\":\"vspeed\",\"kmh\":5");
    (void)cmd("{\"cmd\":\"step\",\"ms\":20");
    const char *a = uds("07 31 01 F0 20 01 4C 4B");
    const bool refused = msg_is(a, "7F 31 22");
    a = uds("05 31 03 F0 20 00");
    expect(refused && msg_is(a, "71 03 F0 20 00 00 08"),
           "service mode refused while the bench VCU reports 5 km/h (NRC 0x22, reason 8: vehicle speed)");
    (void)cmd("{\"cmd\":\"vspeed\",\"kmh\":0");
    (void)cmd("{\"cmd\":\"step\",\"ms\":20");
    const bool unlocked = unlock();
    a = uds("07 31 01 F0 20 01 4C 4B");
    expect(unlocked && msg_is(a, "71 01 F0 20 01"),
           "service mode entered: RoutineControl 0xF020 Rs routine, locked rotor attested (LK), at 0 km/h");
    int polls = 0;
    for (; polls < 100; polls++) {
        (void)cmd("{\"cmd\":\"step\",\"ms\":40");
        a = uds("05 31 03 F0 20 00"); /* the results poll is the tool's heartbeat (hb_timeout_ms 200) */
        if (msg_byte(a, 5) != 1) {
            break;
        }
    }
    char what[200];
    (void)snprintf(what, sizeof what, "the Rs routine ran to DONE under the tool's heartbeat (%d polls, 40 ms apart)",
                   polls + 1);
    expect(msg_is(a, "71 03 F0 20 00 02 00"), what);
    a = uds("05 31 03 F0 20 20"); /* the acks share one line buffer: each read before the next request */
    const bool rs_ok = msg_is(a, "71 03 F0 20 20");
    const double rs = ((msg_byte(a, 5) << 8) | msg_byte(a, 6)) * 1.0e-5; /* 71 03 F0 20 20 [value BE16], 10 uOhm */
    const double rs_plant = jnum(s_hello, "motor.rs_ohm");
    const bool valid = msg_is(uds("05 31 03 F0 20 10"), "71 03 F0 20 10 01");
    (void)snprintf(what, sizeof what, "Rs identified %.2f mOhm, the plant's %.2f mOhm (within 2 %%), verdict VALID",
                   rs * 1.0e3, rs_plant * 1.0e3);
    expect(rs_ok && (rs > (0.98 * rs_plant)) && (rs < (1.02 * rs_plant)) && valid, what);
    (void)cmd("{\"cmd\":\"pause\",\"on\":false");
    (void)cmd("{\"cmd\":\"enable\",\"on\":true");

    send("{\"cmd\":\"speed\",\"rpm\":1500,\"ramp_rpm_s\":5000,\"id\":2}");
    const double rpm = 1450.0;
    wait_tel("dyno at 1500 rpm, the firmware's resolver speed follows", p_speed, &rpm, 2000.0);
    send("{\"cmd\":\"torque\",\"nm\":120,\"id\":3}");
    const double nm = 120.0;
    wait_tel("120 Nm: RUN, modulating, firmware and plant torque within 10 %", p_torque, &nm, 2000.0);

    /* round 23 (item 2): at a 750 V link the back-EMF passes the link near 6 900 rpm, below n_x */
    (void)cmd("{\"cmd\":\"torque\",\"nm\":0");
    (void)cmd("{\"cmd\":\"speed\",\"rpm\":7500,\"ramp_rpm_s\":5000");
    wait_tel("7500 rpm, 750 V, 0 Nm: modulating, pack current within 5 A, no braking torque", p_emf, NULL, 3000.0);
    (void)cmd("{\"cmd\":\"speed\",\"rpm\":1500,\"ramp_rpm_s\":5000");
    const double rpm_back = 1550.0;
    wait_tel("dyno back at 1500 rpm", p_speed_le, &rpm_back, 3000.0);
    (void)cmd("{\"cmd\":\"torque\",\"nm\":120");
    wait_tel("120 Nm again: RUN", p_torque, &nm, 2000.0);

    send("{\"cmd\":\"param_set\",\"name\":\"cal_torque_ramp_nm_s\",\"value\":2500,\"id\":10}");
    a = wait_ack(10);
    expect((a != NULL) && jtrue(a, "ok") && (jnum(a, "value") == 2500.0), "param_set in range: accepted");
    send("{\"cmd\":\"param_set\",\"name\":\"cal_ign_off_v\",\"value\":6,\"id\":11}");
    a = wait_ack(11);
    expect((a != NULL) && !jtrue(a, "ok") && (jnum(a, "violations") >= 1.0),
           "param_set ign_off >= ign_on: refused by ti_params_validate()");

    send("{\"cmd\":\"inject\",\"fault\":\"desat_hs\",\"id\":4}");
    wait_tel("DESAT on a high side: DTC_DESAT_HS active, state FAULT", p_dtc, &desat, 500.0);
    const double t_retry = s_t + 1100.0;
    wait_tel("1.1 s after the DESAT", p_after, &t_retry, 2000.0);
    (void)cmd("{\"cmd\":\"retry_auth\"");
    wait_tel("FW-15: the VCU's retry authorisation, RUN again", p_run, NULL, 2000.0);
    (void)cmd("{\"cmd\":\"inject\",\"fault\":\"desat_hs\"");
    wait_tel("a second DESAT: DTC_DESAT_REPEAT active, state FAULT", p_dtc, &repeat, 500.0);

    /* round 23: the FW-40 services over the bridge's ISO 15765-2 tester */
    a = uds("06 19 04 D1 00 11 FF");
    const int snap = 6 + 53; /* the header (59 04, the DTC, its status), then records of 4 + 49 bytes */
    const float n1 = msg_f32(a, 6 + 14);
    (void)snprintf(what, sizeof what,
                   "0x19 04 DTC_DESAT_HS: both snapshots (%d bytes, %d frames: first frame, flow control, consecutive)",
                   msg_len(a), ack_frames(a));
    expect(msg_is(a, "59 04 D1 00 11") && (msg_len(a) == (6 + (2 * 53))) && (ack_frames(a) >= 2) &&
           (msg_byte(a, 6) == 1) && (msg_byte(a, snap) == 2) && (msg_byte(a, 6 + 12) == 4) &&
           (msg_byte(a, snap + 12) == 4) && (n1 > 1400.0f) && (n1 < 1600.0f), what);
    a = uds("02 19 0A");
    (void)snprintf(what, sizeof what, "0x19 0A: all %d DTCs with their status (%d bytes, %d frames)", n_dtcs,
                   msg_len(a), ack_frames(a));
    expect(msg_is(a, "59 0A 7F D1 00 01") && (msg_len(a) == (3 + (4 * n_dtcs))) && (ack_frames(a) >= 2), what);
    a = uds("03 2A 02 00");
    const bool started = msg_is(a, "6A");
    const int n_per = periodic_until(s_t + 100.0, 0);
    a = uds("03 2A 04 00");
    const bool stopped = msg_is(a, "6A");
    const int n_after = periodic_until(s_t + 100.0, 0);
    (void)snprintf(what, sizeof what, "0x2A: DID 0xF200 every 10 ms on 0x6E9 (%d frames in 100 ms), stopped (%d after)",
                   n_per, n_after);
    expect(started && stopped && (n_per >= 8) && (n_after == 0), what);
    a = cmd("{\"cmd\":\"dtc_clear\"");
    expect((a != NULL) && !jtrue(a, "ok") && (strstr(a, "NRC 0x22") != NULL),
           "dtc_clear with HV present: refused by the image's 0x14 gate (NRC 0x22)");

    send("{\"cmd\":\"torque\",\"nm\":0,\"id\":5}");
    send("{\"cmd\":\"disarm\",\"id\":6}");
    wait_tel("disarm: contactors open, MCU_GATE_EN low", p_disarmed, NULL, 3000.0);
    (void)cmd("{\"cmd\":\"speed\",\"rpm\":0,\"ramp_rpm_s\":5000"); /* a turning rotor holds the link (diode bridge) */
    const double rpm_stop = 5.0;
    wait_tel("dyno stopped", p_speed_le, &rpm_stop, 2000.0);
    (void)cmd("{\"cmd\":\"discharge\"");
    wait_tel("discharge: HV safe (< 60 V)", p_hv_safe, NULL, 3000.0);
    a = cmd("{\"cmd\":\"dtc_clear\"");
    const bool cleared = (a != NULL) && jtrue(a, "ok");
    const double t_clr = s_t + 50.0;
    wait_tel("50 ms after the clear", p_after, &t_clr, 500.0);
    expect(cleared && dtc_active(s_line, desat) && dtc_active(s_line, repeat),
           "dtc_clear through 0x14 (SecurityAccess, HV absent): accepted; the DESAT latches kept");

    send("{\"cmd\":\"provision\",\"sa_key\":false,\"id\":7}");
    a = wait_ack(7);
    expect((a != NULL) && jtrue(a, "ok") && !jtrue(s_hello, "provision.sa_key") && (jpath(s_hello, "provision.sa_key") != NULL),
           "provision sa_key false: power-cycled without the bench key");
    wait_tel("FW-15 at the power-up: the DESAT record blocks arming, FAULT names DTC_DESAT_HS", p_blocked, &desat, 3000.0);
    a = uds("02 27 01");
    expect(msg_is(a, "7F 27 22") && (strstr(a, "\"rsp\":\"03 7F 27 22") != NULL),
           "UDS 27 01 without a key: NRC 0x22 (fail closed), the classic single frame");

    (void)fclose(s_to); /* EOF: the bridge exits */
    while (next_line()) {
    }
    int status = 0;
    (void)waitpid(pid, &status, 0);
    expect(WIFEXITED(status) && (WEXITSTATUS(status) == 0), "stdin closed: the bridge exits with status 0");
    (void)printf("%s: %d failure(s)\n", (s_fails == 0) ? "PASS" : "FAIL", s_fails);
    return (s_fails == 0) ? 0 : 1;
}
