/* hal/gpio.h — discrete I/O of the control card (ball map rev A.12, board_pins.h).
 * Every output here has a board pull-down (default OFF) except ASC_CLR_N (pull-up: "no clear").
 * The ASC_CLR_N output latch is written high before its pin driver is enabled (§9 step 1). */
#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include "ti_types.h"

typedef enum {
    HAL_DO_MCU_GATE_EN = 0, /* P2  PTD16: UAND1.B of the DRV_EN chain */
    HAL_DO_ASC_REQ,         /* T5  PTD6 : ASC latch clock (rising edge sets) */
    HAL_DO_ASC_CLR_N,       /* R8  PTD8 : ASC latch clear, active low */
    HAL_DO_FLT_CLR,         /* U8  PTD9 : fault-latch one-shot, falling edge fires */
    HAL_DO_QDIS,            /* U5  PTD5 : active discharge command */
    HAL_DO_EN_FLYBK_HS,     /* D13 PTD30: flyback HS enable (OR with FS_GPIO1) */
    HAL_DO_EN_FLYBK_LS,     /* C13 PTD31: flyback LS enable (OR with FS_GPIO1) */
    HAL_DO_INTRLOK_P,       /* B4  PTE31: HVIL ladder drive */
    HAL_DO_COUNT
} hal_do_t;

typedef enum {
    HAL_DI_DRV_EN_RB = 0, /* T6  PTD10 */
    HAL_DI_ASC_CMD_RB,    /* U6  PTD11 */
    HAL_DI_RDY_HS,        /* D15 PTB10 */
    HAL_DI_RDY_LS,        /* U2  PTB5  */
    HAL_DI_FLT_HS_N,      /* P15 PTC26 (also eFlexPWM1 FAULT0) */
    HAL_DI_FLT_LS_N,      /* R14 PTC25 (also eFlexPWM1 FAULT2) */
    HAL_DI_SBC_INTB,      /* B12 PTC7  */
    HAL_DI_COUNT
} hal_di_t;

void hal_gpio_write(hal_do_t pin, bool level);
bool hal_gpio_out_state(hal_do_t pin); /* output latch read-back */
bool hal_gpio_read(hal_di_t pin);

/* FW-16 step h pad rule: the FLT pad is pulled low only by enabling its output buffer with the
 * data register held at 0; disable restores input-only and re-locks the pad configuration.
 * Only HAL_DI_FLT_HS_N / HAL_DI_FLT_LS_N are accepted. Returns false for any other pin. */
bool hal_gpio_flt_pad_drive_low(hal_di_t pin, bool enable);

#endif /* HAL_GPIO_H */
