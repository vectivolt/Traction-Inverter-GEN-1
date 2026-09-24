# Traction inverter firmware — host tests and target build notes.
#
#   make test          build + run every host test (cc: clang or gcc), warnings are errors
#   make target-check  syntax-check the S32K396 platform layer on the host (no RTD needed)
#   make target        build for the S32K396 — needs the NXP S32K3 RTD and arm-none-eabi-gcc
#   make params        regenerate include/params_<sku>.h from tools/gen-params.mjs (node)
#   make board-map     regenerate src/platform/s32k396/board_pins.h from the ball map (node)
#   make clean

CC      ?= cc
BUILD   := build
STD     := -std=c11
WARN    := -Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wmissing-prototypes \
           -Wstrict-prototypes -Wundef -Wpointer-arith -Wcast-qual -Wvla
CFLAGS  ?= -O1 -g
INC     := -Iinclude -Isrc/hal -Isrc/util -Isrc/sense -Isrc/control -Isrc/safety -Isrc/comms \
           -Isrc/discharge -Isrc/nvm -Isrc/app

FW_SRC   := $(wildcard src/util/*.c src/sense/*.c src/control/*.c src/safety/*.c src/comms/*.c \
                       src/discharge/*.c src/nvm/*.c src/app/*.c)
HOST_SRC := $(wildcard src/platform/host/*.c)
TEST_SRC := $(wildcard tests/*.c)
TGT_SRC  := $(wildcard src/platform/s32k396/*.c)

HOST_OBJ := $(patsubst %.c,$(BUILD)/host/%.o,$(FW_SRC) $(HOST_SRC) $(TEST_SRC))

.PHONY: all test target target-check params board-map clean

all: test

test: $(BUILD)/host/ti_tests
	./$(BUILD)/host/ti_tests

$(BUILD)/host/ti_tests: $(HOST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ -lm

# test code does its reference arithmetic in double; the firmware itself stays single precision
$(BUILD)/host/tests/%.o: TEST_WARN := -Wno-double-promotion

$(BUILD)/host/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC $<"
	@$(CC) $(STD) $(WARN) $(TEST_WARN) $(CFLAGS) $(INC) -Isrc/platform/host -Itests -MMD -MP -c $< -o $@

-include $(HOST_OBJ:.o=.d)

# The platform layer compiles on the host with the RTD calls compiled out (TI_RTD_AVAILABLE
# undefined): every TODO body keeps its safe default. This catches drift between the HAL headers,
# board_pins.h and the platform code without the NXP SDK.
target-check:
	@for f in $(TGT_SRC); do \
	  $(CC) $(STD) $(WARN) -fsyntax-only -DTI_TARGET_S32K396 $(INC) -Isrc/platform/s32k396 $$f || exit 1; \
	done; echo "target-check: $(words $(TGT_SRC)) platform files OK (RTD calls compiled out)"

# Target build. Required (not in this repository):
#   - NXP S32K3 Real-Time Drivers (RTD) for S32K39x, AUTOSAR 4.7 / IP layer, version >= 4.0.0:
#     Adc_Sar_Ip, Bctu_Ip, Sdadc_Ip, FlexPwm_Ip (eFlexPWM), Trgmux_Ip, Lcu_Ip, Siul2_Port_Ip,
#     Siul2_Dio_Ip, Siul2_Icu_Ip, Lpspi_Ip, FlexCAN_Ip, C40_Ip or Fee, Stm_Ip, Swt_Ip, Fccu_Ip,
#     Clock_Ip, IntCtrl_Ip; plus the S32K39 device headers (S32K39_*.h)
#   - arm-none-eabi-gcc (Cortex-M7, -mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfloat-abi=hard)
#   - a linker script with the .ti_retained no-init section
# Set S32K3_RTD=<path> and run `make target`; see README.md "Target build".
TARGET_CC  ?= arm-none-eabi-gcc
S32K3_RTD  ?=
target:
	@if [ -z "$(S32K3_RTD)" ]; then \
	  echo "make target: set S32K3_RTD to the S32K3 RTD install (see Makefile / README.md)"; exit 2; fi
	@command -v $(TARGET_CC) >/dev/null || { echo "make target: $(TARGET_CC) not found"; exit 2; }
	@mkdir -p $(BUILD)/target
	$(TARGET_CC) $(STD) $(WARN) -Os -mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb \
	  -DTI_TARGET_S32K396 -DTI_RTD_AVAILABLE -DTI_SKU_BUILD=$(or $(SKU),TI_SKU_8XX_SIC) $(INC) \
	  -Isrc/platform/s32k396 -I$(S32K3_RTD)/include -c $(FW_SRC) $(TGT_SRC)
	@echo "compiled; link with the RTD objects and your linker script"

params:
	node tools/gen-params.mjs

board-map:
	node tools/gen-board-map.mjs

clean:
	rm -rf $(BUILD)
