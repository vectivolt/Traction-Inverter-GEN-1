/* s32k396_board_cfg.h — board configuration the S32K39 reference manual must supply.
 *
 * TODO(RM): the SIUL2 IMCR table of the S32K39 reference manual is not in this repository. Fill in
 * the four values below from it:
 *   TI_IMCR_PWM1_FAULT0 / TI_IMCR_PWM1_FAULT2 : IMCR index of the eFlexPWM_1 FAULT0 / FAULT2 inputs;
 *   TI_IMCR_SSS_PTC26   / TI_IMCR_SSS_PTC25   : the SSS value that selects pad PTC26 (FLT_HS_N) /
 *                                               PTC25 (FLT_LS_N) in that IMCR;
 * then define TI_BOARD_IMCR_BOUND. Until then a target build (TI_RTD_AVAILABLE) stops with #error,
 * and every other build treats the route as UNBOUND: hal_pwm_fault_route_bound() is false, nothing
 * is written to any IMCR and the image refuses to arm. If the mux cannot reach FAULT0/FAULT2 from
 * PTC26/PTC25, the contract (FW-15) asks for a card pin swap, not a different firmware route.
 * s32k396.h checks that the filled values are non-zero and that the two IMCR indices differ. */
#ifndef S32K396_BOARD_CFG_H
#define S32K396_BOARD_CFG_H

#define TI_IMCR_UNBOUND 0u

#if defined(TI_BOARD_IMCR_BOUND)
/* the four values, e.g.
 * #define TI_IMCR_PWM1_FAULT0 <index>
 * #define TI_IMCR_PWM1_FAULT2 <index>
 * #define TI_IMCR_SSS_PTC26 <sss>
 * #define TI_IMCR_SSS_PTC25 <sss> */
#else
#define TI_IMCR_PWM1_FAULT0 TI_IMCR_UNBOUND
#define TI_IMCR_PWM1_FAULT2 TI_IMCR_UNBOUND
#define TI_IMCR_SSS_PTC26 TI_IMCR_UNBOUND
#define TI_IMCR_SSS_PTC25 TI_IMCR_UNBOUND
#if defined(TI_RTD_AVAILABLE)
#error "s32k396_board_cfg.h: the IMCR routing of FLT_HS_N/FLT_LS_N to eFlexPWM_1 FAULT0/FAULT2 is unbound (firmware/docs/target-bringup.md)"
#endif
#endif

#endif /* S32K396_BOARD_CFG_H */
