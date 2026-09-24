/* test_crc.c — CRC-8 (FS26 / SAE J1850) and CRC-32 check values; FS26 frame protection. */
#include "fs26_regs.h"
#include "test.h"
#include "ti_crc.h"

TEST(crc8_sae_j1850_check_value)
{
    const uint8_t m[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(ti_crc8_1d(m, 9u, 0xFFu, 0xFFu) == 0x4Bu); /* CRC-8/SAE-J1850 catalogue check */
    CHECK(ti_crc8_1d(m, 9u, 0xFFu, 0x00u) == (uint8_t)(0x4Bu ^ 0xFFu));
}

TEST(crc32_check_value)
{
    const uint8_t m[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(ti_crc32(m, 9u) == 0xCBF43926u);
}

TEST(fs26_frame_every_single_bit_error_detected)
{
    const uint32_t f = fs26_frame(0x4Du, true, 0xA55Au);
    CHECK(fs26_frame_crc_ok(f));
    CHECK((f >> 25) == 0x4Du);
    CHECK(((f >> 24) & 1u) == 1u);
    CHECK(((f >> 8) & 0xFFFFu) == 0xA55Au);
    unsigned missed = 0u;
    for (unsigned b = 0u; b < 32u; b++) {
        missed += fs26_frame_crc_ok(f ^ (1u << b)) ? 1u : 0u;
    }
    CHECK(missed == 0u);
}

void suite_crc(void)
{
    RUN(crc8_sae_j1850_check_value);
    RUN(crc32_check_value);
    RUN(fs26_frame_every_single_bit_error_detected);
}
