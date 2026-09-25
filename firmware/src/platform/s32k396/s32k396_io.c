/* s32k396_io.c — hal/gpio.h, timer.h, wdog.h, spi_fs26.h, can.h and nvm.h on the S32K396.
 *   GPIO   : SIUL2 (Siul2_Dio_Ip for data, MSCR written directly for the §9 step-1 order and the
 *            FW-16 step-h pad rule); pins from board_pins.h;
 *   timer  : STM_0 at 1 MHz (hal_time_us = the counter), channel 0 = the 1 ms task tick;
 *   crit   : PRIMASK save/restore (nestable);
 *   wdog   : SWT_0 (the MCU's own watchdog; the FS26 window watchdog is FW-12, fs26.c);
 *   SPI    : LPSPI3 PCS0, 32-bit FS26 frames, blocking with a bounded timeout (1 ms task only);
 *   CAN    : FlexCAN0 (vehicle) / FlexCAN1 (diagnostic), CAN-FD, RX into a software ring from the
 *            RTD callback, non-blocking TX on one MB per bus;
 *   NVM    : Fee (32 blocks x 512 B) behind a RAM mirror: reads never touch flash after boot, a
 *            write updates the mirror only when Fee reports the job OK. */
#include <string.h>

#include "can.h"
#include "gpio.h"
#include "nvm.h"
#include "s32k396.h"
#include "spi_fs26.h"
#include "timer.h"
#include "wdog.h"

#ifdef TI_RTD_AVAILABLE
#include "Fee.h" /* TODO(RTD): the IP drivers (AUTOSAR Fee/Fls, MemIf types), the Config Tools symbols, half-port
                  * symbols, Fee block numbers, SPI timeout units and FlexCAN DataInfo field names below */
#include "Fls.h"
#include "FlexCAN_Ip.h"
#include "Lpspi_Ip.h"
#include "Siul2_Dio_Ip.h"
#include "Stm_Ip.h"
#include "Swt_Ip.h"
extern const Stm_Ip_InstanceConfigType STM_0_InitConfig_PB;
extern const Stm_Ip_ChannelConfigType STM_0_ChannelConfig_PB[];
extern const Swt_Ip_ConfigType Swt_Ip_Cfg0;
extern const Lpspi_Ip_ConfigType Lpspi_Ip_PhyUnitConfig_SpiPhyUnit_3;
extern const Lpspi_Ip_ExternalDeviceType Lpspi_Ip_DeviceAttributes_FS26; /* 32-bit frames, CPHA 1 */
extern const Flexcan_Ip_ConfigType FlexCAN_Config0, FlexCAN_Config1;
extern const Fee_ConfigType Fee_Config;
extern const Fls_ConfigType Fls_Config;
/* SIUL2 16-pin port halves, index = MSCR / 16 (half-port symbols of the release). */
static Siul2_Dio_Ip_GpioType *const HALF[16] = {PTA_L_HALF, PTA_H_HALF, PTB_L_HALF, PTB_H_HALF, PTC_L_HALF, PTC_H_HALF,
                                                PTD_L_HALF, PTD_H_HALF, PTE_L_HALF, PTE_H_HALF, PTF_L_HALF, PTF_H_HALF,
                                                PTG_L_HALF, PTG_H_HALF, PTH_L_HALF, PTH_H_HALF};
#define TI_FEE_BLOCK(slot) ((uint16_t)(1u + (slot))) /* FeeBlockConfiguration numbers */
#define TI_SPI_TIMEOUT 1000u                          /* units of the release (loops / us) */
#define TI_CAN_TX_MB 0u
#define TI_NVM_BOOT_POLLS 200000u /* bounded synchronous Fee read at boot */
#endif

/* ======================= GPIO ======================= */
static const uint16_t DO_MSCR[HAL_DO_COUNT] = {BP_MCU_GATE_EN_MSCR, BP_ASC_REQ_MSCR,         BP_ASC_CLR_M_MSCR,
                                               BP_FLT_CLR_M_MSCR,   BP_QDIS_M_MSCR,          BP_MCU_EN_FLYBK_HS_MSCR,
                                               BP_MCU_EN_FLYBK_LS_MSCR, BP_INTRLOK_P_MSCR};
static const uint16_t DI_MSCR[HAL_DI_COUNT] = {BP_DRV_EN_RB_MSCR, BP_ASC_CMD_RB_MSCR, BP_RDY_HS_MSCR, BP_RDY_LS_MSCR,
                                               BP_FLT_HS_N_MSCR,  BP_FLT_LS_N_MSCR,   BP_SBC_INTB_MSCR};
/* Reset levels: every output low except ASC_CLR_N (high = "no clear"). */
static const bool DO_INIT[HAL_DO_COUNT] = {false, false, true, false, false, false, false, false};
static bool s_out[HAL_DO_COUNT];

static void pin_write(uint16_t mscr, bool level)
{
#ifdef TI_RTD_AVAILABLE
    Siul2_Dio_Ip_WritePin(HALF[mscr / 16u], (Siul2_Dio_Ip_PinsChannelType)(mscr % 16u),
                          level ? 1u : 0u);
#else
    (void)mscr;
    (void)level;
#endif
}

static bool pin_read(uint16_t mscr)
{
#ifdef TI_RTD_AVAILABLE
    return Siul2_Dio_Ip_ReadPin(HALF[mscr / 16u], (Siul2_Dio_Ip_PinsChannelType)(mscr % 16u)) != 0u;
#else
    (void)mscr;
    return false; /* no pads: FLT reads asserted, RDY/DRV_EN read low -> never armed */
#endif
}

/* §9 step 1: the data latch is written before the output buffer is enabled, so ASC_CLR_N never
 * glitches low and nothing else glitches high. The generated Siul2_Port_Ip configuration must leave
 * OBE off for these pins (TODO(RTD)). */
bool s32k_gpio_init(void)
{
    for (uint32_t i = 0u; i < (uint32_t)HAL_DO_COUNT; i++) {
        pin_write(DO_MSCR[i], DO_INIT[i]);
        s_out[i] = DO_INIT[i];
    }
    for (uint32_t i = 0u; i < (uint32_t)HAL_DO_COUNT; i++) {
        SIUL2_MSCR(DO_MSCR[i]) = TI_MSCR_OBE; /* SSS 0 = GPIO */
    }
    for (uint32_t i = 0u; i < (uint32_t)HAL_DI_COUNT; i++) {
        SIUL2_MSCR(DI_MSCR[i]) = TI_MSCR_IBE; /* FLT pads: IMCR also routes them to eFlexPWM FAULT0/2 */
    }
#ifdef TI_RTD_AVAILABLE
    return true;
#else
    return false;
#endif
}

void hal_gpio_write(hal_do_t pin, bool level)
{
    if ((uint32_t)pin < (uint32_t)HAL_DO_COUNT) {
        pin_write(DO_MSCR[pin], level);
        s_out[pin] = level;
    }
}

bool hal_gpio_out_state(hal_do_t pin)
{
    if ((uint32_t)pin >= (uint32_t)HAL_DO_COUNT) {
        return false;
    }
#ifdef TI_RTD_AVAILABLE
    const uint16_t m = DO_MSCR[pin];
    return ((Siul2_Dio_Ip_GetPinsOutput(HALF[m / 16u]) >> (m % 16u)) & 1u) != 0u; /* the latch itself */
#else
    return s_out[pin];
#endif
}

bool hal_gpio_read(hal_di_t pin) { return ((uint32_t)pin < (uint32_t)HAL_DI_COUNT) ? pin_read(DI_MSCR[pin]) : false; }

bool hal_gpio_flt_pad_drive_low(hal_di_t pin, bool enable)
{
    if ((pin != HAL_DI_FLT_HS_N) && (pin != HAL_DI_FLT_LS_N)) {
        return false;
    }
    const uint16_t m = DI_MSCR[pin];
    pin_write(m, false); /* the data register is held at 0: the pad can only be pulled low */
    SIUL2_MSCR(m) = enable ? (TI_MSCR_IBE | TI_MSCR_OBE) : TI_MSCR_IBE;
    /* TODO(RTD/HW-RM): re-apply the REG_PROT soft lock on this MSCR after the disable. */
    return (SIUL2_MSCR(m) & TI_MSCR_OBE) == (enable ? TI_MSCR_OBE : 0u);
}

/* ======================= timer / critical section ======================= */
bool s32k_timer_init(void)
{
#ifdef TI_RTD_AVAILABLE
    /* TODO(RTD): STM_0 prescaler for TI_STM_HZ; channel 0 compare = CNT + 1000 re-armed in its ISR */
    Stm_Ip_Init(TI_STM_INST, &STM_0_InitConfig_PB);
    Stm_Ip_InitChannel(TI_STM_INST, &STM_0_ChannelConfig_PB[0]);
    Stm_Ip_StartTimer(TI_STM_INST, 0u);
    Stm_Ip_StartCounting(TI_STM_INST, 0u, 1000u);
    return true;
#else
    return false;
#endif
}

uint32_t hal_time_us(void) { return STM_CNT(); }

/* A12-R06: the STM counter extended to 64 bits (timer.h). Read at least once per 71.6 min: the
 * 1 ms task reads it every tick. PRIMASK makes the read-and-extend atomic for every caller. */
static ti_time64_t s_t64;

uint64_t hal_time_us64(void)
{
    hal_crit_enter();
    const uint64_t t = ti_time64_extend(&s_t64, STM_CNT());
    hal_crit_exit();
    return t;
}

void hal_delay_us(uint32_t us)
{
    const uint32_t t0 = hal_time_us();
    /* bounded even if the STM stopped: a few hundred reads per microsecond at most */
    for (uint32_t n = 0u; (n < ((us * 400u) + 1000u)) && ((uint32_t)(hal_time_us() - t0) < us); n++) {
    }
}

static uint32_t s_crit_depth;
static uint32_t s_crit_primask;

void hal_crit_enter(void)
{
#if defined(__arm__)
    uint32_t pm;
    __asm volatile("mrs %0, primask" : "=r"(pm));
    __asm volatile("cpsid i" ::: "memory");
    if (s_crit_depth == 0u) {
        s_crit_primask = pm;
    }
#endif
    s_crit_depth++;
}

void hal_crit_exit(void)
{
    if (s_crit_depth == 0u) {
        return;
    }
    s_crit_depth--;
#if defined(__arm__)
    if ((s_crit_depth == 0u) && (s_crit_primask == 0u)) {
        __asm volatile("cpsie i" ::: "memory");
    }
#else
    (void)s_crit_primask;
#endif
}

/* ======================= SWT ======================= */
bool hal_wdog_init(uint32_t timeout_ms)
{
#ifdef TI_RTD_AVAILABLE
    /* TODO(RTD): Swt_Ip_Cfg0 timeout = timeout_ms (SWT clock x ms / 1000), reset on timeout, keyed
     * service; must match the value the application passes (50 ms). */
    (void)timeout_ms;
    return Swt_Ip_Init(TI_SWT_INST, &Swt_Ip_Cfg0) == SWT_IP_STATUS_SUCCESS;
#else
    (void)timeout_ms;
    return false;
#endif
}

void hal_wdog_kick(void)
{
#ifdef TI_RTD_AVAILABLE
    Swt_Ip_Service(TI_SWT_INST);
#endif
}

/* ======================= FS26 SPI ======================= */
bool hal_fs26_spi_init(void)
{
#ifdef TI_RTD_AVAILABLE
    return Lpspi_Ip_Init(&Lpspi_Ip_PhyUnitConfig_SpiPhyUnit_3) == LPSPI_IP_STATUS_SUCCESS;
#else
    return false;
#endif
}

bool hal_fs26_xfer(uint32_t tx, uint32_t *rx)
{
#ifdef TI_RTD_AVAILABLE
    uint32_t in = 0u;
    const Lpspi_Ip_StatusType st = Lpspi_Ip_SyncTransmit(&Lpspi_Ip_DeviceAttributes_FS26, (const uint8 *)&tx,
                                                         (uint8 *)&in, 4u, TI_SPI_TIMEOUT);
    *rx = in;
    return st == LPSPI_IP_STATUS_SUCCESS;
#else
    (void)tx;
    *rx = 0u;
    return false;
#endif
}

/* ======================= CAN ======================= */
#define CAN_RING 16u
typedef struct {
    hal_can_frame_t f[CAN_RING];
    volatile uint32_t head; /* written by the RX callback */
    uint32_t tail;          /* read by the 1 ms task */
    uint32_t overrun;
} can_ring_t;
static can_ring_t s_rx[2];

/* RX callback body (FlexCAN_Ip RX-complete event): copy into the ring; a full ring drops the new
 * frame and counts it (the E2E alive counter then shows the loss). */
static void can_rx_push(uint8_t bus, uint32_t id, uint8_t len, const uint8_t *data)
{
    can_ring_t *r = &s_rx[bus & 1u];
    if ((r->head - r->tail) >= CAN_RING) {
        r->overrun++;
        return;
    }
    hal_can_frame_t *f = &r->f[r->head % CAN_RING];
    f->id = id;
    f->len = (len <= HAL_CAN_MAX_LEN) ? len : HAL_CAN_MAX_LEN;
    (void)memcpy(f->data, data, f->len);
    r->head = r->head + 1u;
}

#ifdef TI_RTD_AVAILABLE
static Flexcan_Ip_StateType s_can_state[2];
static Flexcan_Ip_MsgBuffType s_can_mb[2];
/* TODO(RTD): registered in FlexCAN_ConfigN as the Callback; RX MB 1 (filter 0x101, 0x102 on bus 0; the UDS
 * request 0x7E1 on bus 1, FW-32). */
void s32k_can_callback(uint8 instance, Flexcan_Ip_EventType ev, uint32 mb, const Flexcan_Ip_StateType *st);
void s32k_can_callback(uint8 instance, Flexcan_Ip_EventType ev, uint32 mb, const Flexcan_Ip_StateType *st)
{
    (void)st;
    if (ev == FLEXCAN_EVENT_RX_COMPLETE) {
        can_rx_push(instance, s_can_mb[instance & 1u].msgId, s_can_mb[instance & 1u].dataLen,
                    s_can_mb[instance & 1u].data);
        (void)FlexCAN_Ip_Receive(instance, (uint8)mb, &s_can_mb[instance & 1u], FALSE); /* re-arm */
    }
}
#endif

bool hal_can_init(uint8_t bus)
{
    if (bus > HAL_CAN_DIAG) {
        return false;
    }
    (void)memset(&s_rx[bus], 0, sizeof s_rx[bus]);
#ifdef TI_RTD_AVAILABLE
    const Flexcan_Ip_ConfigType *cfg = (bus == HAL_CAN_VEHICLE) ? &FlexCAN_Config0 : &FlexCAN_Config1;
    if (FlexCAN_Ip_Init(bus, &s_can_state[bus], cfg) != FLEXCAN_STATUS_SUCCESS) {
        return false;
    }
    /* TODO(RTD): FlexCAN_Ip_ConfigRxMb() for the IDs of this bus, then FlexCAN_Ip_Receive() once */
    (void)FlexCAN_Ip_Receive(bus, 1u, &s_can_mb[bus], FALSE);
    return FlexCAN_Ip_SetStartMode(bus) == FLEXCAN_STATUS_SUCCESS;
#else
    (void)can_rx_push;
    return false;
#endif
}

bool hal_can_rx(uint8_t bus, hal_can_frame_t *f)
{
    if (bus > HAL_CAN_DIAG) {
        return false;
    }
    can_ring_t *r = &s_rx[bus];
    if (r->head == r->tail) {
        return false;
    }
    *f = r->f[r->tail % CAN_RING];
    r->tail++;
    return true;
}

bool hal_can_tx(uint8_t bus, const hal_can_frame_t *f)
{
    if ((bus > HAL_CAN_DIAG) || (f->len > HAL_CAN_MAX_LEN)) {
        return false;
    }
#ifdef TI_RTD_AVAILABLE
    const Flexcan_Ip_DataInfoType info = {.msg_id_type = FLEXCAN_MSG_ID_STD,
                                          .data_length = can_dlc_to_len(can_len_to_dlc(f->len)),
                                          .fd_enable = TRUE,
                                          .enable_brs = TRUE,
                                          .is_polling = TRUE};
    if (FlexCAN_Ip_GetTransferStatus(bus, TI_CAN_TX_MB) == FLEXCAN_STATUS_BUSY) {
        return false; /* never waits: the next 10 ms status frame supersedes this one */
    }
    return FlexCAN_Ip_Send(bus, TI_CAN_TX_MB, &info, f->id, f->data) == FLEXCAN_STATUS_SUCCESS;
#else
    return false;
#endif
}

/* ======================= NVM (Fee behind a RAM mirror) ======================= */
static uint8_t s_mirror[HAL_NVM_SLOTS][HAL_NVM_SLOT_SIZE];
static bool s_mirror_ok[HAL_NVM_SLOTS];
static bool s_mirror_loaded;
static uint8_t s_wbuf[HAL_NVM_SLOT_SIZE]; /* Fee reads from here until the job ends */
static uint16_t s_wslot;
static bool s_wbusy;

#ifdef TI_RTD_AVAILABLE
static bool fee_wait(void)
{
    for (uint32_t n = 0u; n < TI_NVM_BOOT_POLLS; n++) {
        Fee_MainFunction();
        Fls_MainFunction();
        if (Fee_GetStatus() == MEMIF_IDLE) {
            return Fee_GetJobResult() == MEMIF_JOB_OK;
        }
    }
    return false;
}
#endif

static void mirror_load(void)
{
    s_mirror_loaded = true;
#ifdef TI_RTD_AVAILABLE
    Fls_Init(&Fls_Config);
    Fee_Init(&Fee_Config);
    (void)fee_wait();
    for (uint16_t s = 0u; s < HAL_NVM_SLOTS; s++) {
        s_mirror_ok[s] = (Fee_Read(TI_FEE_BLOCK(s), 0u, s_mirror[s], HAL_NVM_SLOT_SIZE) == E_OK) && fee_wait();
    }
#else
    (void)memset(s_mirror_ok, 0, sizeof s_mirror_ok);
#endif
}

bool hal_nvm_read(uint16_t slot, void *buf, uint32_t len)
{
    if (!s_mirror_loaded) {
        mirror_load(); /* first call is nv_init() at boot, before the ISRs run */
    }
    if ((slot >= HAL_NVM_SLOTS) || (len > HAL_NVM_SLOT_SIZE) || !s_mirror_ok[slot]) {
        return false;
    }
    (void)memcpy(buf, s_mirror[slot], len);
    return true;
}

bool hal_nvm_write_start(uint16_t slot, const void *buf, uint32_t len)
{
    if (s_wbusy || (slot >= HAL_NVM_SLOTS) || (len > HAL_NVM_SLOT_SIZE)) {
        return false;
    }
    (void)memset(s_wbuf, 0xFF, sizeof s_wbuf);
    (void)memcpy(s_wbuf, buf, len);
    s_wslot = slot;
#ifdef TI_RTD_AVAILABLE
    if (Fee_Write(TI_FEE_BLOCK(slot), s_wbuf) != E_OK) {
        return false;
    }
    s_wbusy = true;
    return true;
#else
    return false;
#endif
}

hal_nvm_status_t hal_nvm_poll(void)
{
    if (!s_wbusy) {
        return HAL_NVM_IDLE;
    }
#ifdef TI_RTD_AVAILABLE
    Fee_MainFunction(); /* bounded per call by the Fee configuration: background loop only */
    Fls_MainFunction();
    if (Fee_GetStatus() != MEMIF_IDLE) {
        return HAL_NVM_BUSY;
    }
    s_wbusy = false;
    if (Fee_GetJobResult() != MEMIF_JOB_OK) {
        return HAL_NVM_DONE_ERR;
    }
#else
    s_wbusy = false;
#endif
    (void)memcpy(s_mirror[s_wslot], s_wbuf, HAL_NVM_SLOT_SIZE);
    s_mirror_ok[s_wslot] = true;
    return HAL_NVM_DONE_OK;
}
