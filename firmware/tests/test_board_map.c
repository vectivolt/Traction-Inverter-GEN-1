/* test_board_map.c — the generated S32K396 binding table against what the firmware assumes. */
#include <string.h>

#include "../src/platform/s32k396/board_pins.h"
#include "test.h"

static const bp_entry_t PINS[] = BOARD_PINS_INIT;

static const bp_entry_t *pin(const char *net)
{
    for (unsigned i = 0u; i < BOARD_PIN_COUNT; i++) {
        if (strcmp(PINS[i].net, net) == 0) {
            return &PINS[i];
        }
    }
    return NULL;
}

TEST(phase_currents_on_three_adc_instances)
{
    const bp_entry_t *u = pin("ISNS_U");
    const bp_entry_t *v = pin("ISNS_V");
    const bp_entry_t *w = pin("ISNS_W");
    CHECK(u && v && w);
    CHECK(u->kind == BP_ADC && v->kind == BP_ADC && w->kind == BP_ADC);
    CHECK(u->inst == 3u && v->inst == 4u && w->inst == 0u); /* simultaneous sampling possible */
    CHECK(u->inst != v->inst && v->inst != w->inst && u->inst != w->inst);
}

TEST(vdc_channels_on_separate_instances)
{
    const bp_entry_t *a = pin("VDC1_SE");
    const bp_entry_t *b = pin("VDC2_SE");
    CHECK(a && b && a->inst == 6u && b->inst == 1u && a->inst != b->inst);
    CHECK(strcmp(pin("VOFS")->fn, "ADC4_S11") == 0);
    CHECK(strcmp(pin("V5GD_SNS")->port, "PTD27") == 0); /* the contract text still says PTB5 */
    CHECK(strcmp(pin("RDY_LS")->port, "PTB5") == 0);
}

TEST(pwm_submodules_and_fault_inputs)
{
    const char *hi[3] = {"PWM_UH", "PWM_VH", "PWM_WH"};
    const char *lo[3] = {"PWM_UL", "PWM_VL", "PWM_WL"};
    for (unsigned i = 0u; i < 3u; i++) {
        const bp_entry_t *h = pin(hi[i]);
        const bp_entry_t *l = pin(lo[i]);
        CHECK(h && l && h->kind == BP_PWM && l->kind == BP_PWM);
        CHECK(h->inst == 1u && l->inst == 1u && h->chan == i && l->chan == i && h->sub == 'A' && l->sub == 'B');
    }
    const bp_entry_t *fh = pin("FLT_HS_N");
    const bp_entry_t *fl = pin("FLT_LS_N");
    CHECK(fh && fh->kind == BP_PWM_FAULT && fh->inst == 1u && fh->chan == 0u && strcmp(fh->port, "PTC26") == 0);
    CHECK(fl && fl->kind == BP_PWM_FAULT && fl->inst == 1u && fl->chan == 2u && strcmp(fl->port, "PTC25") == 0);
}

TEST(resolver_spi_can_bindings)
{
    CHECK(pin("VREXM_P")->inst == 1u && pin("VREXM_N")->inst == 1u);
    CHECK(pin("SIN_P")->inst == 2u && pin("SIN_N")->inst == 2u && pin("COS_P")->inst == 3u && pin("COS_N")->inst == 3u);
    CHECK(pin("SWG1")->kind == BP_SWG && pin("SWG1")->inst == 1u);
    CHECK(pin("SBC_MOSI")->inst == 3u && pin("SBC_MISO")->inst == 3u && pin("SBC_SCK")->inst == 3u &&
          pin("SBC_CS")->inst == 3u);
    CHECK(pin("CAN0_TX")->kind == BP_CAN && pin("CAN0_RX")->inst == 0u && pin("CAN1_TX")->inst == 1u);
    CHECK(pin("FCCU_ERR0")->kind == BP_FCCU && pin("FCCU_ERR1")->kind == BP_FCCU);
}

TEST(safety_gpio_mscr_indices)
{
    CHECK(BP_MCU_GATE_EN_MSCR == 3u * 32u + 16u); /* PTD16 */
    CHECK(BP_ASC_REQ_MSCR == 3u * 32u + 6u);
    CHECK(BP_ASC_CLR_M_MSCR == 3u * 32u + 8u);
    CHECK(BP_FLT_CLR_M_MSCR == 3u * 32u + 9u);
    CHECK(BP_DRV_EN_RB_MSCR == 3u * 32u + 10u);
    CHECK(BP_ASC_CMD_RB_MSCR == 3u * 32u + 11u);
    CHECK(BP_QDIS_M_MSCR == 3u * 32u + 5u);
    CHECK(BOARD_PIN_COUNT == 63u);
}

void suite_board_map(void)
{
    RUN(phase_currents_on_three_adc_instances);
    RUN(vdc_channels_on_separate_instances);
    RUN(pwm_submodules_and_fault_inputs);
    RUN(resolver_spi_can_bindings);
    RUN(safety_gpio_mscr_indices);
}
