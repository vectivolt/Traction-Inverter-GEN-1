# Traction Tool simulator bridge: the firmware's host build (firmware/src + firmware/src/platform/host), unmodified,
# linked with the bridge (scheduler, plant, VCU/BMS model, JSON on stdio). Compiled with the firmware Makefile's host
# flags (-std=c11 and its warning set, warnings are errors); objects live here, never under firmware/.
#   make            build ./sim_bridge
#   make check      build and run the stdio self-test (check.c): boot, arm, torque, a DESAT, its DTC, disarm
#   make clean
FW      := ../../firmware
CC      ?= cc
BUILD   := build
STD     := -std=c11
WARN    := -Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wmissing-prototypes \
           -Wstrict-prototypes -Wundef -Wpointer-arith -Wcast-qual -Wvla
CFLAGS  ?= -O2 -g -ffp-contract=off # the firmware modules round as on the M7 (firmware/Makefile)
# every firmware module directory (round 23: src/diag, src/boot and any later module are picked up automatically — the
# module list mirrors firmware/Makefile's FW_SRC: all of src/*/ except platform, plus the host platform)
FW_MODS := $(filter-out $(FW)/src/platform/,$(sort $(dir $(wildcard $(FW)/src/*/))))
INC     := -I$(FW)/include $(patsubst %/,-I%,$(FW_MODS)) -I$(FW)/src/platform/host
FW_SRC  := $(wildcard $(addsuffix *.c,$(FW_MODS)) $(FW)/src/platform/host/*.c)
BR_SRC  := bridge.c plant.c json.c
OBJ     := $(patsubst $(FW)/%.c,$(BUILD)/fw/%.o,$(FW_SRC)) $(patsubst %.c,$(BUILD)/%.o,$(BR_SRC))

.PHONY: all check clean
all: sim_bridge

sim_bridge: $(OBJ)
	@$(CC) $(CFLAGS) -o $@ $^ -lm
	@echo "  LD $@"

$(BUILD)/check: check.c
	@mkdir -p $(BUILD)
	@$(CC) $(STD) $(WARN) $(CFLAGS) -o $@ $<

check: sim_bridge $(BUILD)/check
	@./$(BUILD)/check ./sim_bridge

$(BUILD)/fw/%.o: $(FW)/%.c
	@mkdir -p $(dir $@)
	@echo "  CC firmware/$*.c"
	@$(CC) $(STD) $(WARN) $(CFLAGS) $(INC) -MMD -MP -c $< -o $@

# DTC names in enum order, from the firmware's dtc.h (bridge.c checks the count against DTC_COUNT at compile time)
$(BUILD)/dtc_names.h: $(FW)/src/comms/dtc.h
	@mkdir -p $(BUILD)
	@awk '/typedef enum/ {on=1; next} on && /} dtc_id_t;/ {on=0} on && match($$0, /^[ \t]*DTC_[A-Z0-9_]+/) { \
	  n = substr($$0, RSTART, RLENGTH); gsub(/[ \t]/, "", n); if (n != "DTC_COUNT") printf "    \"%s\",\n", n }' $< \
	  | { echo "/* GENERATED from firmware/src/comms/dtc.h by tool/bridge/Makefile */"; \
	      echo "static const char *const DTC_NAMES[] = {"; cat; echo "};"; } > $@

$(BUILD)/bridge.o: $(BUILD)/dtc_names.h

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC bridge/$<"
	@$(CC) $(STD) $(WARN) $(CFLAGS) $(INC) -I$(BUILD) -MMD -MP -c $< -o $@

-include $(OBJ:.o=.d)

clean:
	rm -rf $(BUILD) sim_bridge
