# Two binaries, named after the processor they need:
#   make               MPY20.TTP  68020 or better (STE + TwiSTer, TT, Falcon)
#   make CPU=68000     MPY00.TTP  any machine, stock ST/STE included
# Each has its own build directory, so both can be kept side by side.
#   make SSL=1         MPY20SSL.TTP  MPY20 plus TLS (mbedTLS): ssl, tls,
#                      HTTPS for requests; ~250 KB more, hence a binary of
#                      its own
CPU ?= 68020
SSL ?= 0
ifeq ($(SSL),1)
ifneq ($(CPU),68020)
$(error SSL=1 needs CPU=68020)
endif
BUILD ?= build-ssl
MPY_TTP = MPY20SSL.TTP
MICROPY_PY_SSL = 1
MICROPY_SSL_MBEDTLS = 1
FROZEN_MANIFEST ?= $(shell pwd)/manifest_ssl.py
else ifeq ($(CPU),68000)
BUILD ?= build-68000
MPY_TTP = MPY00.TTP
else ifeq ($(CPU),68020)
MPY_TTP = MPY20.TTP
else
$(error CPU must be 68020 or 68000)
endif

include ../../py/mkenv.mk

# Cross-compiler for Atari TOS. We use the modern ELF fork
# (m68k-atari-mintelf-, gcc 13): its linker emits a GEMDOS PRG directly, no
# conversion step needed. Vincent Riviere's older toolchain is m68k-atari-mint-.
# If the toolchain is not on PATH, we pick it up from MINT_BIN_DIR.
MINT_BIN_DIR ?= /opt/cross-mint/bin
ifneq ($(wildcard $(MINT_BIN_DIR)/m68k-atari-mintelf-gcc),)
CROSS_COMPILE ?= $(MINT_BIN_DIR)/m68k-atari-mintelf-
else
CROSS_COMPILE ?= m68k-atari-mintelf-
endif

QSTR_DEFS = qstrdefsport.h

MICROPY_ROM_TEXT_COMPRESSION ?= 1

FROZEN_MANIFEST ?= $(shell pwd)/manifest.py

# The port uses MICROPY_OBJ_REPR_B, whose small integers hold 30 bits, not the
# 31 mpy-cross assumes by default. Tell it, so a frozen constant that needs 31
# bits is boxed rather than silently truncated.
MPY_CROSS_FLAGS += -msmall-int-bits=30

include $(TOP)/py/py.mk
include $(TOP)/extmod/extmod.mk

INC += -I.
INC += -I$(TOP)
INC += -I$(BUILD)

# Floating point. A TwiSTer-equipped STE has a 68020 and NO FPU, and a stock
# Falcon030 has none either (the 68882 is a socket, usually empty), so soft
# float is the default.
#
# The toolchain has exactly two library variants (gcc -print-multi-lib): the
# default one, built for 68000 without FPU, and m68020-60, which assumes a
# 68881 -- its MiNTlib startup checks for one and refuses to run without.
# There is no 68020-without-FPU variant, so we split the flags: our own code
# is compiled for the 68020 without FPU instructions, and the link picks the
# default (soft float) libraries. 68000 library code runs fine on a 68020.
#
#   make            STE + TwiSTer, TT, stock Falcon  -> soft float
#   make FPU=1      machine with a 68881/68882       -> hard float
#
# The 68020 build checks the _CPU cookie at startup (and the _FPU cookie when
# built with FPU=1) and refuses to run rather than crash on a stock 68000
# ST/STE. CPU=68000 is always soft float.
FPU ?= 0
ifeq ($(CPU),68000)
ifeq ($(FPU),1)
$(error CPU=68000 has no FPU variant)
endif
CPU_FLAGS ?= -m68000 -msoft-float
LD_CPU_FLAGS ?= -m68000 -msoft-float
else ifeq ($(FPU),1)
CPU_FLAGS ?= -m68020-60
LD_CPU_FLAGS ?= -m68020-60
else
CPU_FLAGS ?= -m68020 -msoft-float
LD_CPU_FLAGS ?= -msoft-float
endif

# -Os rather than -O2: it saves ~570 KB of text, which is real money on an
# Atari, and costs nothing measurable on a bytecode interpreter. Both levels
# were run against tests/SMOKE.PY under Hatari with identical results.
# (An earlier version of this comment blamed -Os for a miscompile; the real
# bug was MICROPY_OBJ_BASE_ALIGNMENT, see mpconfigport.h.)
OPT_FLAGS ?= -Os

# Per-file optimisation override, for pinning down a file that misbehaves at
# one level:  make OPT_FLAGS=-Os VM_OPT=-O2
VM_OPT ?=
ifneq ($(VM_OPT),)
$(BUILD)/py/vm.o: CFLAGS += $(VM_OPT)
endif
CFLAGS += $(INC) -Wall -std=gnu99 $(CPU_FLAGS) $(OPT_FLAGS) $(COPT)
CFLAGS += -fdata-sections -ffunction-sections
CFLAGS += -Wno-error

LDFLAGS += $(LD_CPU_FLAGS) -Wl,--gc-sections
# gemlib (MiNTlib): VDI and AES for modgem.c
LIBS += -lgem -lm

CSUPEROPT = -Os

SRC_C = \
	main.c \
	cpucheck.c \
	atari_mphal.c \
	modatarios.c \
	machine_atari.c \
	machine_mem.c \
	machine_uart.c machine_sound.c machine_disk.c modgem.c \
	modzlib.c \
	modsocket.c \
	shared/readline/readline.c \
	shared/runtime/pyexec.c \
	shared/runtime/gchelper_generic.c \
	shared/runtime/interrupt_char.c \
	shared/runtime/stdout_helpers.c \
	shared/timeutils/timeutils.c \

ifeq ($(SSL),1)
SRC_C += mbedtls/mbedtls_port.c
# mbedTLS itself: third-party code, its warnings are not ours to fix
$(BUILD)/lib/mbedtls/%.o: CFLAGS += -Wno-all
endif

SRC_QSTR += shared/readline/readline.c shared/runtime/pyexec.c main.c modzlib.c
SRC_QSTR += modatarios.c machine_atari.c machine_mem.c machine_uart.c
SRC_QSTR += machine_sound.c machine_disk.c modgem.c modsocket.c

# MiNTlib already provides printf and abort.
SRC_EXTMOD_C := $(filter-out shared/libc/%.c,$(SRC_EXTMOD_C))

OBJ += $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

# Auto-incrementing build number and timestamp.
# .build_number persists across cleans (not in build/).
BUILD_NUMBER_FILE = .build_number
BUILD_VERSION_HDR = $(BUILD)/genhdr/atariversion.h

$(BUILD_VERSION_HDR): FORCE | $(BUILD)/genhdr
	$(Q)if [ ! -f $(BUILD_NUMBER_FILE) ]; then echo 0 > $(BUILD_NUMBER_FILE); fi
	$(Q)echo $$(( $$(cat $(BUILD_NUMBER_FILE)) + 1 )) > $(BUILD_NUMBER_FILE)
	$(Q)echo "// Auto-generated - do not edit" > $@
	$(Q)echo "#define ATARI_BUILD_NUM \"$$(cat $(BUILD_NUMBER_FILE))\"" >> $@
	$(Q)echo "#define ATARI_BUILD_TIMESTAMP \"$$(date '+%Y-%m-%d %H:%M')\"" >> $@

# cpucheck.c is what tells the user that MPY20.TTP cannot run on a 68000, so
# it must itself be 68000 code whatever the build (the last -m option wins).
$(BUILD)/cpucheck.o: CFLAGS += -m68000
ifeq ($(CPU),68020)
$(BUILD)/cpucheck.o: CFLAGS += -DATARI_BUILD_68020
endif

PROG = mpy

include $(TOP)/py/mkrules.mk

# All objects include mpconfigport.h which includes atariversion.h.
# Must be after mkrules.mk so $(OBJ) is fully expanded.
$(BUILD)/main.o: $(BUILD_VERSION_HDR)
$(BUILD)/genhdr/qstr.i.last: $(BUILD_VERSION_HDR)

# TOS wants the .TTP extension so the desktop offers a command line prompt.
all: $(BUILD)/$(MPY_TTP)

# GEMDOS PRGFLAGS. The mintelf linker already sets 0x07 (bit 0 fastload,
# bit 1 load the program itself in alt-RAM, bit 2 Malloc may return alt-RAM);
# we set them explicitly so the value is visible and easy to change.
# For the alt-RAM/DMA experiment on real hardware, build with PRGFLAGS=0x01:
# everything then stays in ST-RAM except our own Mxalloc'd GC heap.
PRGFLAGS ?= 0x07

$(BUILD)/$(MPY_TTP): $(BUILD)/$(PROG)
	$(Q)cp $< $@
	$(Q)$(CROSS_COMPILE)flags -f $(PRGFLAGS) $@
	$(ECHO) "Ready: $@ (PRGFLAGS=$(PRGFLAGS))"

# --- Precompiled native code -------------------------------------------------
# A .mpy containing native code (@native, @viper, @asm_m68k) must be
# produced by an mpy-cross that shares the port's object representation,
# REPR_B: native code bakes in the encoding of constants. The ordinary
# mpy-cross refuses -march=m68k for that reason.
#   make mpy-cross-atari
#   ../../mpy-cross/build-atari/mpy-cross -march=m68k -msmall-int-bits=30 x.py
#   make natmod            (tests/natmod.py -> build/NATMOD.MPY, for NATTEST)
MPY_CROSS_ATARI = $(TOP)/mpy-cross/build-atari/mpy-cross

mpy-cross-atari: $(MPY_CROSS_ATARI)

$(MPY_CROSS_ATARI):
	$(MAKE) -C $(TOP)/mpy-cross BUILD=build-atari \
	    CFLAGS_EXTRA="-DMICROPY_OBJ_REPR=MICROPY_OBJ_REPR_B"

natmod: $(BUILD)/NATMOD.MPY

$(BUILD)/NATMOD.MPY: tests/natmod.py $(MPY_CROSS_ATARI)
	$(MPY_CROSS_ATARI) -march=m68k -msmall-int-bits=30 -o $@ $<

.PHONY: mpy-cross-atari natmod
