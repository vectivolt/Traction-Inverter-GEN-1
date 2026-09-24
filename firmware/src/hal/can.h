/* hal/can.h — FlexCAN0 (vehicle, R13/U14) and FlexCAN1 (diagnostic, D3/C3), CAN-FD. */
#ifndef HAL_CAN_H
#define HAL_CAN_H

#include "ti_types.h"

#define HAL_CAN_VEHICLE 0u
#define HAL_CAN_DIAG 1u
#define HAL_CAN_MAX_LEN 64u

typedef struct {
    uint32_t id;
    uint8_t len;
    uint8_t data[HAL_CAN_MAX_LEN];
} hal_can_frame_t;

bool hal_can_init(uint8_t bus);
bool hal_can_rx(uint8_t bus, hal_can_frame_t *f); /* non-blocking pop */
bool hal_can_tx(uint8_t bus, const hal_can_frame_t *f);

#endif /* HAL_CAN_H */
