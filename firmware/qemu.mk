# qemu.mk — the host test suite on the target instruction set: Cortex-M7 code from arm-none-eabi-gcc, run on QEMU's
# mps2-an500 (a Cortex-M7) with semihosted stdio and files. Included by the Makefile's last line; the file sets are
# the Makefile's own FW_SRC, HOST_SRC, TEST_SRC and TGT_SRC, compiled with its STD, WARN and INC.
#
#   make qemu-test    tests/ + src/ with the host platform (the `make test` set) for the M7, run under QEMU; passes
#                     only on exit 0 and the line "N tests, M checks, 0 failed"
#   make target-size  src/ + the s32k396 platform, RTD calls compiled out (as target-check), for the M7: text/data/bss
#
# ARM_CC       a newlib-equipped arm-none-eabi-gcc (librdimon for semihosting). Homebrew's arm-none-eabi-gcc formula
#              ships no newlib and cannot link the image; the Arm GNU Toolchain (gcc-arm-embedded cask) can.
# M7_FPU       fpv5-sp-d16: the S32K396's M7 has a single-precision FPU (docs/datasheets/S32K39.pdf, data sheet
#              rev. 3 §3.2 "IEEE 754-compliant SPFPU"; `make target` uses the same). Double arithmetic — only the
#              tests' reference arithmetic has any — then runs in libgcc's soft-float, as it would on the target.
#              QEMU's cortex-m7 also has the double-precision unit: M7_FPU=fpv5-d16 runs on that instead, but that is
#              not the target: newlib then links other sinf/cosf/expf/logf (double-based), whose last bits differ.
# QEMU_TIMEOUT seconds before QEMU is killed (the suite takes minutes under TCG).
#
# Excluded tests: none. Where newlib (4.5, as built for arm-none-eabi) differs from the host C library:
#   system()  newlib is built NO_EXEC (system() only returns -1); qemu/startup.c routes it to librdimon's semihosting
#             SYS_SYSTEM, which QEMU runs in the host shell from make's directory. So capture:
#             the_blocks_decode_to_the_waveform_through_the_node_decoder decodes the M7's image with the host's
#             node tools/capture-decode.mjs, as on the host (node is needed here too).
#   timespec_get  missing (no TIME_UTC); qemu/shim.h supplies it on the semihosted calendar clock (SYS_TIME, whole
#             seconds). capture: the_isr_copy_is_a_bounded_copy_on_the_host times cap_isr with it, so the time it
#             measures here is 0 and its `best < 1000.0` half holds trivially — emulated time is not M7 time either
#             way (T-43 is the silicon measurement). Its other half, the capture still ARMED after 800 000 calls, runs.

ARM_CC       ?= $(or $(lastword $(wildcard /Applications/ArmGNUToolchain/*/arm-none-eabi/bin/arm-none-eabi-gcc)),arm-none-eabi-gcc)
ARM_SIZE     ?= $(patsubst %gcc,%size,$(ARM_CC))
M7_FPU       ?= fpv5-sp-d16
M7           := -mcpu=cortex-m7 -mfpu=$(M7_FPU) -mfloat-abi=hard -mthumb -O2
QEMU         ?= qemu-system-arm
QEMU_TIMEOUT ?= 3600
QEMU_ARGS    := -M mps2-an500 -cpu cortex-m7 -nographic -semihosting-config enable=on,target=native
QEMU_DIR     := $(BUILD)/qemu-$(M7_FPU)
QEMU_OBJ     := $(patsubst %.c,$(QEMU_DIR)/%.o,$(FW_SRC) $(HOST_SRC) $(TEST_SRC) qemu/startup.c)
M7T_DIR      := $(BUILD)/m7-target-$(M7_FPU)
M7T_OBJ      := $(patsubst %.c,$(M7T_DIR)/%.o,$(FW_SRC) $(TGT_SRC))

# macOS has no timeout(1) and QEMU ignores SIGALRM: perl forks QEMU and sends it SIGTERM after QEMU_TIMEOUT s
QEMU_RUN = perl -e '$$t = shift; defined($$p = fork) or die "fork: $$!\n"; $$p or exec(@ARGV) or die "exec: $$!\n"; \
  $$SIG{ALRM} = sub { $$late = 1; kill "TERM", $$p }; alarm $$t; waitpid($$p, 0); \
  print "\nqemu-test: killed after $$t s\n" if $$late; exit($$late ? 124 : ($$? & 127) ? 128 + ($$? & 127) : $$? >> 8)' $(QEMU_TIMEOUT)

.PHONY: qemu-test target-size

qemu-test: $(QEMU_DIR)/ti_tests.elf
	@command -v $(QEMU) >/dev/null || { echo "qemu-test: $(QEMU) not found (macOS: brew install qemu)"; exit 2; }
	@echo "qemu-test: $(QEMU) $(QEMU_ARGS) -kernel $< (timeout $(QEMU_TIMEOUT) s)"
	@s=$$(date +%s); { $(QEMU_RUN) $(QEMU) $(QEMU_ARGS) -kernel $< </dev/null 2>&1; echo $$? > $(QEMU_DIR)/rc; } \
	  | tee $(QEMU_DIR)/run.log; rc=$$(cat $(QEMU_DIR)/rc); \
	  line=$$(grep -E '^[0-9]+ tests, [0-9]+ checks, [0-9]+ failed$$' $(QEMU_DIR)/run.log); \
	  echo "qemu-test: QEMU exit $$rc after $$(( $$(date +%s) - s )) s wall ($(M7_FPU))"; \
	  case "$$rc:$$line" in "0:"*" 0 failed") ;; *) echo "qemu-test: FAILED"; exit 1;; esac

$(QEMU_DIR)/ti_tests.elf: $(QEMU_OBJ) qemu/mps2-an500.ld
	@$(ARM_CC) $(M7) --specs=rdimon.specs -nostartfiles -Tqemu/mps2-an500.ld -o $@ $(QEMU_OBJ) -lm
	@$(ARM_SIZE) $@

$(QEMU_DIR)/tests/%.o: TEST_WARN := -Wno-double-promotion
$(QEMU_DIR)/tests/test_capture.o: QEMU_SHIM := -include qemu/shim.h

$(QEMU_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  M7 $<"
	@$(ARM_CC) $(STD) $(WARN) $(TEST_WARN) $(M7) $(QEMU_SHIM) $(INC) -Isrc/platform/host -Itests -MMD -MP -c $< -o $@

ARM_OBJDUMP  ?= $(patsubst %gcc,%objdump,$(ARM_CC))
# 64-bit software division (libgcc __aeabi_(u)ldivmod, a ~100-cycle loop) is allowed only where no ISR runs: the task
# and UDS objects below, and hal_pwm_init (start-up arithmetic in the platform file that also holds the PWM fault path).
# Every other function must not reference it — the ISRs stamp times and locate SDADC blocks through ti_udiv64_16
# (hal/timer.h; round 23, T-36). The check reads each object's disassembly, so it names the offending function.
ULDIV_ALLOWED    := commission.o update.o uds_diag.o runstats.o nvlog.o
ULDIV_ALLOWED_FN := hal_pwm_init
target-size: $(M7T_OBJ)
	@$(ARM_SIZE) -t $^
	@bad=""; for o in $^; do b=$$(basename $$o); case " $(ULDIV_ALLOWED) " in *" $$b "*) continue;; esac; \
	  for fn in $$($(ARM_OBJDUMP) -d $$o | awk '/^[0-9a-f]+ <.*>:$$/{f=$$2} /__aeabi_u?ldivmod/{print f}' | tr -d '<>:' | sort -u); do \
	    case " $(ULDIV_ALLOWED_FN) " in *" $$fn "*) ;; *) bad="$$bad $$b:$$fn";; esac; done; done; \
	  if [ -n "$$bad" ]; then echo "target-size: FAIL — 64-bit software division (__aeabi_(u)ldivmod) in an ISR-capable function:$$bad (T-36)"; exit 1; fi; \
	  echo "target-size: __aeabi_(u)ldivmod only in $(ULDIV_ALLOWED) and $(ULDIV_ALLOWED_FN) (no ISR path)"
	@echo "target-size: $(words $^) objects, $(M7) -DTI_TARGET_S32K396, RTD calls compiled out; libc/libm, the RTD,"
	@echo "             the start-up and the stacks are not in these figures"

$(M7T_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(ARM_CC) $(STD) $(WARN) $(M7) -DTI_TARGET_S32K396 $(INC) -Isrc/platform/s32k396 -MMD -MP -c $< -o $@

-include $(QEMU_OBJ:.o=.d) $(M7T_OBJ:.o=.d)
