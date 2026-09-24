/* test_platform_cfg.c — the S32K396 register images the platform layer writes (s32k396_cfg.h):
 * the FW-15/§4c fault lock-down, the PWM counts/edges/mode images, ADC channel indices and
 * watchdog thresholds, CAN-FD lengths. The hardware effect of these values is a target item. */
#include "../src/platform/s32k396/s32k396_cfg.h"
#include "test.h"

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

void suite_platform_cfg(void)
{
    RUN(fault_lock_image);
    RUN(pwm_counts_and_edges);
    RUN(pwm_mode_images);
    RUN(adc_indices_and_thresholds);
    RUN(can_fd_lengths);
}
