/* test_platform_cfg.c — the S32K396 register images the platform layer writes (s32k396_cfg.h):
 * the FW-15/§4c fault lock-down, the PWM counts/edges/mode images, ADC channel indices and
 * watchdog thresholds, CAN-FD lengths. The hardware effect of these values is a target item. */
#include "../src/platform/s32k396/s32k396.h" /* s32k396_cfg.h + the board configuration */
#include "pwm.h"
#include "sim.h"
#include "test.h"
#include "timer.h"

TEST(fault_lock_image)
{
    const uint16_t f = pwm_fault_fctrl();
    CHECK((f & 0x000Fu) == 0x7u);          /* FIE: interrupt on FAULT0..2 */
    CHECK(((f >> 4) & 0xFu) == 0x7u);      /* FSAFE: fail-safe on all three */
    CHECK(((f >> 8) & 0xFu) == 0x0u);      /* FAUTO 0: manual clear only */
    CHECK(((f >> 12) & 0xFu) == PWM_F1);   /* FLT pins active low, ADC watchdog active high */
    const uint16_t d = pwm_fault_dismap0();
    CHECK((d & 0xFu) == 0x7u);             /* A (high sides): every fault */
    CHECK(((d >> 4) & 0xFu) == 0x5u);      /* B (low sides): FLT_HS/FLT_LS only: PWM-ASC survives FAULT1 */
    CHECK(PWM_FCTRL2_VALUE == 0u);         /* combinational path kept */
    CHECK(pwm_fsts_fflag(0x0F05u) == 0x5u && pwm_fsts_ffpin(0x0A00u) == 0xAu);
    CHECK(pwm_ctrl2(0u) == 0u && ((pwm_ctrl2(1u) >> 13) & 1u) == 0u); /* complementary, SM1/2 slaved */
}

TEST(pwm_counts_and_edges)
{
    const pwm_counts_t c = pwm_counts(160000000u, 10000u, 1000u);
    CHECK(c.ok && c.half == 8000u && c.init == -8000 && c.val1 == 7999 && c.dtcnt == 160u);
    CHECK(pwm_counts(160000000u, 5000u, 2500u).dtcnt == 400u);
    CHECK(pwm_counts(160000001u, 10000u, 1000u).dtcnt == 161u); /* rounded up: never shorter */
    CHECK(!pwm_counts(160000000u, 2000u, 1000u).ok);           /* 40000 counts: needs a prescaler */
    CHECK(!pwm_counts(160000000u, 10000u, 20000u).ok);         /* dead time beyond DTCNT */
    int16_t a;
    int16_t b;
    pwm_edges(0.5f, 8000u, &a, &b);
    CHECK(a == -4000 && b == 4000);
    pwm_edges(0.0f, 8000u, &a, &b);
    CHECK(a == 8000 && b == 8000); /* never set */
    pwm_edges(NAN, 8000u, &a, &b);
    CHECK(a == 8000 && b == 8000);
    pwm_edges(1.0f, 8000u, &a, &b);
    CHECK(a == -8000 && b == 8000); /* set at INIT, never cleared */
}

TEST(pwm_mode_images)
{
    const pwm_mode_img_t off = pwm_mode_image(0u);
    CHECK(off.mask == 0x0770u && off.dtsrcsel == 0u);
    const pwm_mode_img_t asc = pwm_mode_image(1u);
    CHECK(asc.mask == 0x0700u);                  /* high sides masked, low sides free */
    CHECK(asc.dtsrcsel == 0x0888u && asc.swcout == 0u); /* A source = SWCOUT 0 -> B = 1 after dead time */
    const pwm_mode_img_t mod = pwm_mode_image(2u);
    CHECK(mod.mask == 0u && mod.dtsrcsel == 0u);
}

TEST(adc_indices_and_thresholds)
{
    CHECK(S32K3_ADC_CH('P', 1u) == 1u && S32K3_ADC_CH('S', 19u) == 51u);
    CHECK(adc_thrh(3000u) == 2999u); /* strict compare: data > 2999 <=> code >= 3000 */
    CHECK(adc_thrl(0u) == 0u && adc_thrl(100u) == 101u);
}

TEST(can_fd_lengths)
{
    CHECK(can_dlc_to_len(8u) == 8u && can_dlc_to_len(9u) == 12u && can_dlc_to_len(15u) == 64u);
    CHECK(can_len_to_dlc(8u) == 8u && can_len_to_dlc(9u) == 9u && can_len_to_dlc(64u) == 15u);
}

/* F02: "locked" is decided from lock bits read back, never from a matching image, never by default. */
TEST(regprot_lock_decision_needs_every_bit_read_back)
{
    const regprot_reg_t regs[3] = {{0x18Cu, 2u}, {0x18Eu, 2u}, {0x00Cu, 2u}}; /* two share one SLBR */
    CHECK(regprot_slbr_index(0x18Cu) == 0x63u && regprot_slbr_index(0x18Eu) == 0x63u);
    CHECK(regprot_slb_bits(0x18Cu, 2u) == 0x3u && regprot_slb_bits(0x18Eu, 2u) == 0xCu);
    CHECK(regprot_slbr_lock_value(0x18Eu, 2u) == 0xCCu); /* WE only for its own bytes */
    uint8_t rd[3] = {0u, 0u, 0u};
    CHECK(!regprot_locked(0u, rd, regs, 3u));                 /* nothing read back */
    CHECK(!regprot_locked(TI_REGPROT_GCR_HLB, rd, regs, 3u)); /* hard lock, no soft locks */
    rd[0] = 0x3u;
    rd[1] = 0xCu;
    rd[2] = 0x3u;
    CHECK(!regprot_locked(0u, rd, regs, 3u));                 /* soft locks, no hard lock */
    CHECK(regprot_locked(TI_REGPROT_GCR_HLB, rd, regs, 3u));
    rd[1] = 0x4u;                                             /* one byte of one register open */
    CHECK(!regprot_locked(TI_REGPROT_GCR_HLB, rd, regs, 3u));
    CHECK(!regprot_locked(TI_REGPROT_GCR_HLB, rd, regs, 0u)); /* an empty list proves nothing */
}

/* F01: the placeholders are UNBOUND outside a filled board configuration (a target build stops at
 * the TODO(RM) #error); the host platform reports the route unbound, and an unbound route really
 * does not carry a FLT pad to the PWM fault input. */
TEST(fault_route_unbound_by_default)
{
    CHECK(!(TI_IMCR_ROUTE_BOUND) && (TI_IMCR_PWM1_FAULT0 == TI_IMCR_UNBOUND) && (TI_IMCR_SSS_PTC25 == TI_IMCR_UNBOUND));
    CHECK(hal_pwm_init(10000u, 1000u) && hal_pwm_config_matches());
    CHECK(!hal_pwm_fault_route_bound());
    sim_chain_desat(true, false);
    sim_advance_us(5u);
    CHECK((hal_pwm_fault_flags() & HAL_PWM_FAULT_FLT_HS) == 0u);
    sim_pwm_fault_route_bind(true); /* the board configuration filled */
    sim_chain_desat(true, false);
    sim_advance_us(5u);
    CHECK(hal_pwm_fault_route_bound() && ((hal_pwm_fault_flags() & HAL_PWM_FAULT_FLT_HS) != 0u));
}

/* F02 on the host REG_PROT model: after init the lock-down registers reject writes from an
 * application context (CPU) and from a DMA-like master, the outputs keep following the locked map,
 * and after a watchdog (RSTB) reset they are open only until the firmware locks them again. */
TEST(protected_registers_reject_writes_from_cpu_and_dma)
{
    sim_pwm_fault_route_bind(true);
    CHECK(hal_pwm_init(10000u, 1000u));
    CHECK(hal_pwm_config_matches() && hal_pwm_protection_locked());
    CHECK(!sim_pwm_reg_write(SIM_PWM_DISMAP0_SM0, 0x0000u, SIM_MASTER_CPU)); /* a stray pointer */
    CHECK(!sim_pwm_reg_write(SIM_PWM_FCTRL, 0x0000u, SIM_MASTER_DMA));       /* an eDMA descriptor */
    CHECK(!sim_pwm_reg_write(SIM_PWM_OCTRL_SM1, 0x0030u, SIM_MASTER_CPU));
    CHECK(sim_pwm_reg_read(SIM_PWM_DISMAP0_SM0) == pwm_fault_dismap0() && sim_pwm_reg_read(SIM_PWM_FCTRL) == pwm_fault_fctrl());
    CHECK(hal_pwm_config_matches());
    /* CTRL2 (FORCE is written at every mode change) cannot be locked: an INDEP change lands, and the
     * read-back that runs every tick sees it */
    CHECK(sim_pwm_reg_write(SIM_PWM_CTRL2_SM1, (uint16_t)(pwm_ctrl2(1u) | (1u << 13)), SIM_MASTER_CPU));
    CHECK(!hal_pwm_config_matches() && hal_pwm_protection_locked());
    CHECK(sim_pwm_reg_write(SIM_PWM_CTRL2_SM1, pwm_ctrl2(1u), SIM_MASTER_CPU) && hal_pwm_config_matches());
    const float d[3] = {0.5f, 0.4f, 0.6f};
    hal_pwm_set_duty(d);
    sim_chain_desat(false, false);
    sim_advance_us(2u);
    CHECK(sim_pwm_hs_forced_off() && sim_pwm_ls_forced_off()); /* FAULT2 still takes all six off */
    sim_fs26_mcu_reset(); /* the FS26 watchdog reaction: RSTB resets the MCU */
    CHECK(!hal_pwm_protection_locked() && !hal_pwm_config_matches());
    CHECK(sim_pwm_reg_write(SIM_PWM_FFILT, 0x0005u, SIM_MASTER_CPU)); /* open until the firmware runs */
    CHECK(hal_pwm_init(10000u, 1000u) && hal_pwm_protection_locked());
    CHECK(!sim_pwm_reg_write(SIM_PWM_FFILT, 0x0005u, SIM_MASTER_DMA));
    CHECK(sim_pwm_reg_read(SIM_PWM_FFILT) == PWM_FFILT_VALUE && hal_pwm_config_matches());
}

void suite_platform_cfg(void)
{
    RUN(fault_lock_image);
    RUN(pwm_counts_and_edges);
    RUN(pwm_mode_images);
    RUN(adc_indices_and_thresholds);
    RUN(can_fd_lengths);
    RUN(regprot_lock_decision_needs_every_bit_read_back);
    RUN(fault_route_unbound_by_default);
    RUN(protected_registers_reject_writes_from_cpu_and_dma);
}
