# ============================================================
# D213 Dashboard — Refactored Makefile
# Platform: ArtInChip D211 (RISC-V 64bit, Buildroot Linux 5.10)
# Target:  cross-compile (board) / sim (PC SDL2)
# ============================================================

# ── Toolchain ──────────────────────────────────────────────
CROSS_COMPILE  ?= riscv64-unknown-linux-gnu-
CC              = $(CROSS_COMPILE)gcc
STRIP           = $(CROSS_COMPILE)strip

# ── SDK / Library Paths ─────────────────────────────────────
SDK_ROOT       ?= /opt/artinchip/d21x-sdk
LVGL_ROOT      ?= $(CURDIR)/../lvgl
SYSROOT        ?= $(SDK_ROOT)/sysroot
RUNTIME_PATH   ?= /usr/local/lib

# ── Source Files ────────────────────────────────────────────
SRC_DIRS  := drivers ui media core
SRCS      := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
SRCS      += $(wildcard core/*.c)
OBJS      := $(SRCS:.c=.o)

# ── Compiler Flags ──────────────────────────────────────────
CFLAGS   := -std=c11 -Wall -Wextra -O2
CFLAGS   += -Wno-unused-parameter
CFLAGS   += -march=rv64imafdc -mabi=lp64d
CFLAGS   += -I$(LVGL_ROOT)
CFLAGS   += -I$(SDK_ROOT)/include
CFLAGS   += -Iconfigs

# Linker flags
LDFLAGS  := -Wl,-rpath=$(RUNTIME_PATH)
LDLIBS   := -llvgl -lmedia_player -lasound -lmpp_decoder -lpthread -lm

TARGET   := dashboard_d213

# ── Simulator (SDL2 on PC) ──────────────────────────────────
SIM_CC     = gcc
SIM_CFLAGS = -std=c11 -Wall -Wextra -O0 -g
SIM_CFLAGS += -DLV_SIMULATOR
SIM_CFLAGS += -I$(LVGL_ROOT) -Iconfigs
SIM_CFLAGS += $(shell sdl2-config --cflags)
SIM_LDLIBS = -llvgl -lSDL2 -lSDL2_image -lm -lpthread
SIM_TARGET = dashboard_sim

# ── Targets ─────────────────────────────────────────────────
.PHONY: all clean sim help

all: $(TARGET)
	@echo "Build complete: $(TARGET)"

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)
	$(STRIP) $@
	@echo "  STRIP   $@"
	@ls -lh $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<
	@echo "  CC      $@"

# ── Simulator Build ─────────────────────────────────────────
sim: $(SIM_TARGET)
	@echo "Simulator build complete: $(SIM_TARGET)"
	@echo "Run: ./$(SIM_TARGET)"

$(SIM_TARGET): $(SRCS)
	$(SIM_CC) $(SIM_CFLAGS) -o $@ $^ $(SIM_LDLIBS)
	@ls -lh $@

# ── Clean ───────────────────────────────────────────────────
clean:
	@rm -f $(OBJS) $(TARGET) $(SIM_TARGET)
	@echo "Clean complete"

# ── Help ────────────────────────────────────────────────────
help:
	@echo "D213 Dashboard Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make          Cross-compile for D211 board"
	@echo "  make sim      Build PC simulator (SDL2)"
	@echo "  make clean    Remove build artifacts"
	@echo ""
	@echo "Variables:"
	@echo "  CROSS_COMPILE   RISC-V cross compiler prefix"
	@echo "  SDK_ROOT        ArtInChip SDK path"
	@echo "  LVGL_ROOT       LVGL source path"
