# Halo CE PS3 (PSL1GHT). Build in the ps3dev Docker image: ./build.sh [pkg|self|check|bringup|clean]
# Set LOG_IP to the PC that listens for the UDP log (nc -ul 18194). See port/ps3/README.md.
ifeq ($(strip $(PS3DEV)),)
$(error "PS3DEV is not set: build with ./build.sh")
endif
PSL1GHT ?= $(PS3DEV)
.DEFAULT_GOAL := all

TARGET     := halo_ps3
TITLE      := Halo CE PS3
TITLE_ID   := HALOCE000
CONTENT_ID := UP0001-$(TITLE_ID)_00-0000000000000000
BUILD      ?= build/ps3
LOG_IP     ?= 192.168.178.99
# XMB icon, 320x176 PNG; the PSL1GHT default until port/ps3/icon/ICON0.PNG exists
ICON0      ?= $(firstword $(wildcard port/ps3/icon/ICON0.PNG) $(PS3DEV)/bin/ICON0.PNG)

PREFIX          := $(PS3DEV)/ppu/bin/ppu-
CC              := $(PREFIX)gcc
NM              := $(PREFIX)nm
READELF         := $(PREFIX)readelf
SPRXLINK        := $(PS3DEV)/bin/sprxlinker
FSELF           := $(PS3DEV)/bin/fself
MAKE_SELF_NPDRM := $(PS3DEV)/bin/make_self_npdrm
SFO             := $(PS3DEV)/bin/sfo
PKG             := $(PS3DEV)/bin/pkg

PS3_DIR  := port/ps3
TOML_DIR := port/third_party/tomlc17
MUSL_DIR := port/third_party/musl-math

# ---------- the host layer (port/ps3/src): lv2 and newlib with their own 64-bit long

HOST_SRCS := $(wildcard $(PS3_DIR)/src/*.c)
HOST_ASMS := $(wildcard $(PS3_DIR)/src/*.S)
HOST_OBJS := $(patsubst %,$(BUILD)/obj/%.o,$(basename $(HOST_SRCS) $(HOST_ASMS)))

# -O2 only: -O3 broke boot on this toolchain (ioQuake3-PS3). The include folder
# goes first so that its setjmp.h shadows newlib's.
CFLAGS := -std=gnu99 -O2 -Wall -mcpu=cell -mhard-float -mno-altivec -fno-strict-aliasing \
          -D__PS3__ -DPS3_LOG_IP=\"$(LOG_IP)\" \
          -I$(PS3_DIR)/include -I$(PS3_DIR)/src -I$(PSL1GHT)/ppu/include -I$(PS3DEV)/portlibs/ppu/include
ASFLAGS := -mcpu=cell -mno-altivec

# --no-multi-toc: a TOC over 64 KB must fail the link; split-TOC r2off stubs hang the PS3 at boot.
# --wrap: a retail PS3 has no console for stdout, so printf and co. go to the UDP log.
LDFLAGS := -Wl,--no-multi-toc -L$(PSL1GHT)/ppu/lib -L$(PS3DEV)/portlibs/ppu/lib \
           -Wl,--wrap,printf -Wl,--wrap,vprintf -Wl,--wrap,puts -Wl,--wrap,putchar

LIBS := -lrsx -lgcm_sys -lio -laudio -lsysutil -lsysmodule -lnet -lnetctl -lrt -llv2 -lm

# ---------- the game's units (GOALS.md G1, G2)

GAME_MK   := $(BUILD)/game.mk
SEMANTICS := $(BUILD)/halo_msvc_semantics.h
GAME_WIDE := wcslen wcsnlen wcschr wcsrchr wcscmp wcsncmp wcscpy wcsncpy wcscat wcsncat \
             wmemchr wmemcmp wmemcpy wmemmove wmemset

# The MSVC ABI of LINUX_ABI_FLAGS (tools/linux_build.py) where GCC has it; PPC64 aligns 64-bit
# members as MSVC does, and -fsigned-char undoes its unsigned char. GAME_EXTRA adds flags (-fsyntax-only).
GAME_ABI = -O2 -mcpu=cell -mhard-float -mno-altivec -fsigned-char -fms-extensions -fshort-wchar -fcommon \
           -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks -ffp-contract=off \
           $(addprefix -fno-builtin-,$(GAME_WIDE))
# -mminimal-toc: the game's units need 137 KB of TOC, the one TOC holds 64 KB; each gets its own
GAME_WARN ?= -w
GAME_CFLAGS = -std=gnu89 -D__STRICT_ANSI__ $(GAME_WARN) $(GAME_ABI) -mminimal-toc \
              -include $(PS3_DIR)/include/halo_ps3_prefix.h -include $(SEMANTICS) \
              -I$(PS3_DIR)/include -Iport/linux/include $(GAME_DEFINES) $(GAME_INCLUDES) \
              -idirafter port/include/xdk $(GAME_UNIT_FLAGS) $(GAME_EXTRA)

# the game's main() is called by the PS3's (port/ps3/src/ps3_main.c)
$(BUILD)/obj/source/shell/shell_xbox.o: GAME_UNIT_FLAGS := -Dmain=halo_xbox_main
# prototypes libtiff's units lack, kept out of its licensed headers
$(BUILD)/obj/source/bitmaps/libtiff/%.o: GAME_UNIT_FLAGS := -include $(PS3_DIR)/include/halo_ps3_libtiff.h

# the source path can hold spaces, so the shell gets it quoted
define game_compile
@mkdir -p $(dir $@)
@echo "CC $<"
@$(CC) $(GAME_CFLAGS) -MMD -MP -c "$<" -o $@
endef

$(GAME_MK): port/linux/port.json tools/ps3_game_mk.py
	@python3 tools/ps3_game_mk.py --config port/linux/port.json --build $(BUILD) --output $@

-include $(GAME_MK)

# rewritten only when the scan finds new tags or inline functions
$(SEMANTICS): FORCE
	@python3 tools/ps3_msvc_semantics.py --output $@ --tags source --inlines source --inlines port/include/xdk

$(GAME_OBJS): | $(SEMANTICS)
# the XDK declarations are a system folder (-idirafter), which the depfiles leave out
XDK_HEADERS := $(wildcard port/include/xdk/*.h)
$(GAME_OBJS): $(XDK_HEADERS)

game: $(GAME_OBJS)

# ---------- the Xbox layer: port/ps3/xbox, and the Linux build's units that need no host change

PLATFORM_SEMANTICS := $(BUILD)/platform_msvc_semantics.h
XBOX_SRCS := $(wildcard $(PS3_DIR)/xbox/*.c) \
             $(addprefix port/linux/src/,bink_null.c d3d8_resources.c halo_linker_common.c msvc_crt.c \
             msvc_wide.c port_config.c xbdm.c xbox_xapi.c)
XBOX_OBJS := $(patsubst %.c,$(BUILD)/obj/%.o,$(XBOX_SRCS))

# G3 ends after 30 s of the first map on its swapped data (port/ps3/xbox/xbox_tag_swap.c); G4 drops this
XBOX_DEFINES ?= -DHALO_PS3_STOP_AFTER_MAIN_MENU

# the game's ABI and the XDK declarations; ps3_platform.h stands in for port/linux/src/platform.h.
# -Wno-format: %ld meets the 32-bit long, which PPC64 passes extended to 64 bits, so it prints right
XBOX_CFLAGS = -std=gnu99 -fgnu89-inline $(GAME_ABI) -mminimal-toc -Wall -Wno-unused-function \
              -Wno-unknown-pragmas -Wno-missing-braces -Wno-unused-variable -Wno-format \
              -DHALO_LINUX_PLATFORM_LAYER $(XBOX_DEFINES) \
              -include $(PS3_DIR)/include/halo_ps3_prefix.h -include $(PLATFORM_SEMANTICS) \
              -include $(PS3_DIR)/xbox/ps3_platform.h \
              -I$(PS3_DIR)/include -Iport/linux/include -I$(PS3_DIR)/src -Iport/linux/src -I$(TOML_DIR) \
              -Isource -Isource/cseries -idirafter port/include/xdk

$(PLATFORM_SEMANTICS): FORCE
	@python3 tools/ps3_msvc_semantics.py --output $@ --inlines port/include/xdk

$(XBOX_OBJS): $(BUILD)/obj/%.o: %.c $(XDK_HEADERS) | $(PLATFORM_SEMANTICS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(XBOX_CFLAGS) -MMD -MP -c $< -o $@

# ---------- third-party code: the settings file's parser, and the game's maths (port/include/halo_math.h)

TOML_OBJS := $(BUILD)/obj/$(TOML_DIR)/tomlc17.o
MUSL_SRCS := $(wildcard $(MUSL_DIR)/src/*.c)
MUSL_OBJS := $(patsubst %.c,$(BUILD)/obj/%.o,$(MUSL_SRCS))

$(TOML_OBJS): $(BUILD)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -std=gnu11 -Dstatic_assert=_Static_assert -w -I$(TOML_DIR) -MMD -MP -c $< -o $@

# the same results on every port: no fused multiply-adds
$(MUSL_OBJS): $(BUILD)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) -std=gnu11 -O2 -mcpu=cell -mhard-float -mno-altivec -ffp-contract=off -w -I$(MUSL_DIR)/include \
		-include $(MUSL_DIR)/include/libm.h -MMD -MP -c $< -o $@

# ---------- the program

.PHONY: all self pkg check clean game bringup bringup-self symbols FORCE

all: $(BUILD)/$(TARGET).elf

# the host objects come first, before libc, so ps3_setjmp.o replaces its setjmp
ALL_OBJS = $(HOST_OBJS) $(XBOX_OBJS) $(TOML_OBJS) $(MUSL_OBJS) $(GAME_OBJS)

$(BUILD)/$(TARGET).elf: $(ALL_OBJS)
	@echo "LD $@"
	@$(CC) $(CFLAGS) $(LDFLAGS) $(ALL_OBJS) $(LIBS) -o $@

$(BUILD)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/obj/%.o: %.S
	@mkdir -p $(dir $@)
	@echo "AS $<"
	@$(CC) $(ASFLAGS) -c $< -o $@

# the G0 bring-up test, without the game: build/ps3/halo_ps3_bringup.elf (bringup-self: its EBOOT.BIN)
BRINGUP_OBJS := $(BUILD)/obj/$(PS3_DIR)/bringup/ps3_bringup.o \
                $(filter-out %/ps3_main.o %/ps3_host.o %/ps3_posix.o,$(HOST_OBJS))

$(BUILD)/$(TARGET)_bringup.elf: $(BRINGUP_OBJS)
	@echo "LD $@"
	@$(CC) $(CFLAGS) $(LDFLAGS) $(BRINGUP_OBJS) $(LIBS) -o $@

bringup: $(BUILD)/$(TARGET)_bringup.elf

bringup-self: $(BUILD)/$(TARGET)_bringup.elf
	$(SPRXLINK) $<
	@mkdir -p $(BUILD)/bringup
	$(FSELF) $< $(BUILD)/bringup/EBOOT.BIN
	@echo "Done: $(BUILD)/bringup/EBOOT.BIN"

# fake-signed EBOOT.BIN, to copy by FTP over an installed package
self: $(BUILD)/$(TARGET).elf
	$(SPRXLINK) $<
	$(FSELF) $< $(BUILD)/EBOOT.BIN
	@echo "Done: $(BUILD)/EBOOT.BIN"

pkg: $(BUILD)/$(TARGET).elf
	$(SPRXLINK) $<
	@mkdir -p $(BUILD)/pkg/USRDIR
	$(MAKE_SELF_NPDRM) $< $(BUILD)/pkg/USRDIR/EBOOT.BIN $(CONTENT_ID)
	$(SFO) --title "$(TITLE)" --appid "$(TITLE_ID)" -f $(PS3DEV)/bin/sfo.xml $(BUILD)/pkg/PARAM.SFO
	@cp $(ICON0) $(BUILD)/pkg/ICON0.PNG
	$(PKG) --contentid $(CONTENT_ID) $(BUILD)/pkg/ $(BUILD)/$(TARGET).pkg
	@echo "Done: $(BUILD)/$(TARGET).pkg"

# the link checks to pass before each hardware test (port/ps3/README.md)
check: $(BUILD)/$(TARGET).elf
	@r2off=$$($(NM) $< | grep -c r2off || true); \
	got=$$($(READELF) -SW $< | awk '{ for (i = 1; i <= NF; i++) if ($$i == ".got") print $$(i + 4) }'); \
	echo "r2off stubs: $$r2off (must be 0)"; \
	echo ".got size:   0x$${got:-0} (must be below 0x10000)"; \
	$(READELF) -lW $< | grep -E "^ +LOAD"; \
	test "$$r2off" = 0 && test $$((0x$${got:-0})) -lt 65536

# the functions of a call stack from the log: ./build.sh symbols ADDRESSES="0056ddb8 0056d570"
symbols: $(BUILD)/$(TARGET).elf
	@python3 tools/ps3_addr2func.py $< $(ADDRESSES)

clean:
	@rm -rf $(BUILD)

-include $(HOST_OBJS:.o=.d) $(XBOX_OBJS:.o=.d) $(TOML_OBJS:.o=.d) $(MUSL_OBJS:.o=.d) $(GAME_OBJS:.o=.d) \
	$(BRINGUP_OBJS:.o=.d)
