/* fs26_regs.h — FS26 SPI protocol and the registers FW-12 uses (FS26 DS Rev.3, 17-Nov-2022,
 * docs/datasheets/FS26.pdf: §15.8 frame/CRC, Tables 15/16 register maps, §18 fail-safe registers,
 * §22.6 challenger watchdog, §22.11 FS0B/FS1B release, Tables 198/199).
 *
 * Frame (32 bit, MSB first): [31] M/FS  [30:25] address  [24] R/W (1 = write)  [23:8] data
 * [7:0] CRC-8 (poly 0x1D, seed 0xFF) over bits 31..8. The 7-bit "hex" addresses below include the
 * M/FS bit (0x40.. = fail-safe), so frame = hex << 25. */
#ifndef FS26_REGS_H
#define FS26_REGS_H

#include <stdbool.h>
#include <stdint.h>

/* main logic */
#define FS26_M_DEVICEID 0x00u
#define FS26_M_PROGID 0x01u
#define FS26_M_REG_CTRL1 0x11u /* bit 8 GPIO1HI: request GPIO1 high */
#define FS26_M_REG_CTRL2 0x12u /* bit 8 GPIO1LO: request GPIO1 low */
#define FS26_GPIO1_BIT 0x0100u

/* fail-safe logic */
#define FS26_FS_GRL_FLAGS 0x40u
#define FS26_FS_I_WD_CFG 0x45u
#define FS26_FS_I_NOT_WD_CFG 0x46u
#define FS26_FS_I_FSSM 0x49u
#define FS26_FS_I_NOT_FSSM 0x4Au
#define FS26_FS_WDW_DURATION 0x4Bu
#define FS26_FS_NOT_WDW_DURATION 0x4Cu
#define FS26_FS_WD_ANSWER 0x4Du
#define FS26_FS_WD_TOKEN 0x4Eu
#define FS26_FS_RELEASE_FS0B_FS1B 0x51u
#define FS26_FS_SAFE_IOS_1 0x52u
#define FS26_FS_SAFE_IOS_2 0x53u
#define FS26_FS_STATES 0x57u
#define FS26_FS_LP_REQ 0x58u

/* FS_I_WD_CFG (Table 75/76) */
#define FS26_WD_ERR_LIMIT_SHIFT 14u /* 00=8 01=6 10=4 11=2 */
#define FS26_WD_ERR_LIMIT_2 3u
#define FS26_WD_RFR_LIMIT_SHIFT 11u /* 00=6 01=4 10=2 11=1 */
#define FS26_WD_FS_REACTION_SHIFT 8u /* 10 = RSTB + FS0B (default) */
#define FS26_WD_FS_REACTION_RSTB_FS0B 2u
#define FS26_WD_CFG_WMASK 0xDB00u
#define FS26_WD_ERR_CNT_MASK 0x000Fu

/* FS_I_FSSM (Table 79/80) */
#define FS26_FLT_ERR_CNT_LIMIT_SHIFT 14u
#define FS26_FLT_ERR_REACTION_SHIFT 11u
#define FS26_BACKUP_FS0B_BIT 0x0080u
#define FS26_BACKUP_FS1B_BIT 0x0040u
#define FS26_FSSM_WMASK 0xDAD0u
#define FS26_FSSM_NOT_EXTRA 0x0020u /* bit 5 must be 1 in FS_I_NOT_FSSM */
#define FS26_FLT_ERR_CNT_MASK 0x000Fu

/* FS_WDW_DURATION (Table 81/82) */
#define FS26_WDW_PERIOD_SHIFT 12u /* 0011 = 3 ms */
#define FS26_WDW_PERIOD_3MS 3u
#define FS26_WDW_DC_SHIFT 6u /* 010 = 50 % closed / 50 % open */
#define FS26_WDW_DC_50 2u
#define FS26_WDW_RECOVERY_DEFAULT 0xBu
#define FS26_WDW_WMASK 0xF1CFu

/* FS_SAFE_IOS_1 (Table 93/94) */
#define FS26_IOS1_EXT_RSTB 0x8000u
#define FS26_IOS1_RSTB_EVENT 0x1000u
#define FS26_IOS1_FS0B_DRV 0x0200u
#define FS26_IOS1_FS0B_SNS 0x0100u
#define FS26_IOS1_FS0B_DIAG 0x0080u
#define FS26_IOS1_FS0B_REQ 0x0040u
#define FS26_IOS1_FS1B_DRV 0x0020u
#define FS26_IOS1_FS1B_SNS 0x0010u
#define FS26_IOS1_FS1B_DIAG 0x0008u
#define FS26_IOS1_FS1B_REQ 0x0004u
#define FS26_IOS1_GOTO_INIT 0x0002u

/* FS_SAFE_IOS_2 (Table 95/96): TDELAY 00000 (with FS0B), TDUR 01011 (100 ms) */
#define FS26_IOS2_TDELAY_SHIFT 5u
#define FS26_IOS2_TDUR_100MS 0x0Bu
#define FS26_IOS2_WMASK 0x03FFu

/* FS_STATES (Table 103/104) */
#define FS26_STATES_DBG_MODE 0x2000u
#define FS26_STATES_OTP_CORRUPT 0x1000u
#define FS26_STATES_REG_CORRUPT 0x0800u
#define FS26_STATES_MASK 0x001Fu
#define FS26_STATE_INIT_FS 0x09u
#define FS26_STATE_SAFETY_OUT_NOT_RELEASED 0x0Au
#define FS26_STATE_NORMAL 0x0Bu

/* FS_LP_REQ (Table 105/106): 0xA5 then 0x5A => LPOFF */
#define FS26_LP_PRE_LPOFF 0xA5u
#define FS26_LP_GO_LPOFF 0x5Au

/* RELEASE_FS0B_FS1B[15:13] (Table 199) */
#define FS26_REL_FS0B 0x3u
#define FS26_REL_FS1B 0x6u
#define FS26_REL_BOTH 0x5u

#define FS26_WD_TOKEN_DEFAULT 0x5AB2u

uint32_t fs26_frame(uint8_t hex_addr, bool write, uint16_t data);
bool fs26_frame_crc_ok(uint32_t frame);
uint16_t fs26_wd_answer(uint16_t token);
uint16_t fs26_release_word(uint16_t token, uint8_t select);

#endif /* FS26_REGS_H */
