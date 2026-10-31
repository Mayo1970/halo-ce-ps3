# Halo CE PS3 (PSL1GHT) on ChupathingyCE (chupathingyce/, a submodule). Build in the ps3dev Docker image:
# ./build.sh [pkg|self|check|bringup|clean]. Set LOG_IP to the PC that listens for the UDP log (nc -ul 18194).
# DEBUG=0 is the retail build: no UDP log (no socket, nothing formatted or sent).
ifeq ($(strip $(PS3DEV)),)
$(error "PS3DEV is not set: build with ./build.sh")
endif
PSL1GHT ?= $(PS3DEV)
.DEFAULT_GOAL := all

TARGET     := halo_ps3
TITLE      := Halo: Combat Evolved
TITLE_ID   := HALOCE000
CONTENT_ID := UP0001-$(TITLE_ID)_00-0000000000000000
BUILD      ?= build/ps3
LOG_IP     ?= 192.168.178.99
DEBUG      ?= 1
# XMB icon, 320x176 PNG
ICON0      ?= ICON/ICON0.PNG

PREFIX          := $(PS3DEV)/ppu/bin/ppu-
CC              := $(PREFIX)gcc
NM              := $(PREFIX)nm
READELF         := $(PREFIX)readelf
SPRXLINK        := $(PS3DEV)/bin/sprxlinker
FSELF           := $(PS3DEV)/bin/fself
MAKE_SELF_NPDRM := $(PS3DEV)/bin/make_self_npdrm
SFO             := $(PS3DEV)/bin/sfo
PKG             := $(PS3DEV)/bin/pkg

# the game (a submodule), and the copies of its sources and ours with `long` spelled `int` (the game's LP64 builds)
GAME        := chupathingyce
LP64        := $(BUILD)/lp64
PS3_DIR     := port/ps3
TOML_DIR    := port/third_party/tomlc17
MUSL_DIR    := $(GAME)/port/third_party/musl-math
ZLIB_DIR    := $(GAME)/port/third_party/zlib
KCP_DIR     := port/third_party/kcp
HTTPS_DIR   := $(GAME)/port/console/https
MBEDTLS_DIR := $(GAME)/port/third_party/mbedtls
# the game's version, which Delta Peer's build string carries
GAME_VERSION := $(shell tr -d '\r\n' < $(GAME)/VERSION)

# written before make reads its rules, so that the depfiles' headers are there and current
LP64_SYNC := $(shell python3 tools/ps3_lp64_tree.py --game $(GAME) --out $(LP64) >&2 && echo ok)
ifneq ($(LP64_SYNC),ok)
$(error "tools/ps3_lp64_tree.py failed")
endif

# ---------- the host layer (port/ps3/src): lv2 and newlib with their own 64-bit long

# (not the old tree's ps3_mbedtls.c: the game's port/console/https defines mbedtls_hardware_poll itself)
HOST_SRCS := $(filter-out %/ps3_mbedtls.c,$(wildcard $(PS3_DIR)/src/*.c))
HOST_ASMS := $(wildcard $(PS3_DIR)/src/*.S)
HOST_OBJS := $(patsubst %,$(BUILD)/obj/%.o,$(basename $(HOST_SRCS) $(HOST_ASMS)))

# -O2 only: -O3 broke boot on this toolchain (ioQuake3-PS3). The include folder
# goes first so that its setjmp.h shadows newlib's.
CFLAGS := -std=gnu99 -O2 -Wall -mcpu=cell -mhard-float -mno-altivec -fno-strict-aliasing \
          -D__PS3__ -DPS3_LOG_IP=\"$(LOG_IP)\" \
          -I$(PS3_DIR)/include -I$(PS3_DIR)/src -I$(PS3_DIR)/spu -I$(PSL1GHT)/ppu/include -I$(PS3DEV)/portlibs/ppu/include \
          -iquote $(GAME)/port/linux/src
ifeq ($(DEBUG),1)
CFLAGS += -DPS3_LOG
endif
ASFLAGS := -mcpu=cell -mno-altivec

# --no-multi-toc: a TOC over 64 KB must fail the link; split-TOC r2off stubs hang the PS3 at boot.
# --wrap: printf and co. to the UDP log (a retail PS3 has no console), the heap's calls through its log (ps3_heap.c).
LDFLAGS := -Wl,--no-multi-toc -L$(PSL1GHT)/ppu/lib -L$(PS3DEV)/portlibs/ppu/lib \
           -Wl,--wrap,printf -Wl,--wrap,vprintf -Wl,--wrap,puts -Wl,--wrap,putchar \
           -Wl,--wrap,malloc -Wl,--wrap,calloc -Wl,--wrap,realloc -Wl,--wrap,memalign -Wl,--wrap,_sbrk_r

LIBS := -lrsx -lgcm_sys -lio -laudio -lsysutil -lsysfs -lsysmodule -lnet -lnetctl -lrt -llv2 -lm

# ---------- the game's units

GAME_MK   := $(BUILD)/game.mk
SEMANTICS := $(BUILD)/halo_msvc_semantics.h
GAME_WIDE := wcslen wcsnlen wcschr wcsrchr wcscmp wcsncmp wcscpy wcsncpy wcscat wcsncat \
             wmemchr wmemcmp wmemcpy wmemmove wmemset

# The MSVC ABI of the game's LP64_ABI_FLAGS (tools/lp64_build.py) where GCC has it; PPC64 aligns 64-bit members as
# MSVC does, and -fsigned-char undoes its unsigned char. GAME_EXTRA adds flags (-fsyntax-only).
GAME_ABI = -O2 -mcpu=cell -mhard-float -mno-altivec -fsigned-char -fms-extensions -fshort-wchar -fcommon \
           -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks -ffp-contract=off -DHALO_64BIT \
           $(addprefix -fno-builtin-,$(GAME_WIDE))
# the players the network tables hold: 16, the Xbox's, as on every console build (the game's tools/console_build.py)
GAME_ABI += -DHALO_CONSOLE_PLAYERS=16
# Delta Peer as a client, as the consoles' builds have it (the game's docs/building.md, "Console ports")
GAME_ABI += -DHALO_CONSOLE_DELTA
# the conversions that cut a 64-bit pointer are errors, as in the game's 64-bit builds (lp64_game_flags); GCC's -w
# would hide them too, so the other warnings it has on by default are turned off one by one
GAME_WARN ?= -Werror=int-to-pointer-cast -Werror=pointer-to-int-cast -Werror=int-conversion \
             -Werror=implicit-function-declaration -Wno-incompatible-pointer-types -Wno-discarded-qualifiers \
             -Wno-multichar -Wno-overflow -Wno-pointer-sign
# -mminimal-toc: the game's units need 137 KB of TOC, the one TOC holds 64 KB; each gets its own
GAME_TOC = -mminimal-toc
GAME_CFLAGS = -std=gnu89 -D__STRICT_ANSI__ $(GAME_WARN) $(GAME_ABI) $(GAME_TOC) \
              -include $(LP64)/$(PS3_DIR)/include/halo_ps3_prefix.h -include $(SEMANTICS) \
              -I$(LP64)/$(PS3_DIR)/include -I$(LP64)/port/linux/include -I$(LP64)/port/console/include \
              -iquote $(LP64)/port/linux/game $(GAME_DEFINES) $(GAME_INCLUDES) \
              -idirafter $(LP64)/port/include/xdk $(GAME_UNIT_FLAGS) $(GAME_EXTRA)

# the game's main() is called by the PS3's (port/ps3/src/ps3_main.c)
$(BUILD)/obj/source/shell/shell_xbox.o: GAME_UNIT_FLAGS := -Dmain=halo_xbox_main
# the units with the most calls a tick in the PPE profile (13 KB of TOC): in the one TOC, their small functions save no
# r30 (-mminimal-toc's TOC register), whose restore read its own save back at once (a load-hit-store)
FULL_TOC_UNITS := source/memory/data source/cache/cache_files source/objects/objects source/scenario/scenario \
                  source/game/game_time source/main/main source/math/matrix_math source/math/real_math \
                  source/math/random_math source/physics/collision_bsp source/physics/collision_models \
                  source/physics/collisions source/physics/collision_usage source/physics/breakable_surfaces \
                  source/structures/structures source/models/model_animations source/ai/path source/units/units \
                  source/ai/actor_perception port/linux/game/object_bounds_cache
$(patsubst %,$(BUILD)/obj/%.o,$(FULL_TOC_UNITS)): GAME_TOC :=

# the source path can hold spaces, so the shell gets it quoted
define game_compile
@mkdir -p $(dir $@)
@echo "CC $<"
@$(CC) $(GAME_CFLAGS) -MMD -MP -c "$<" -o $@
endef

$(GAME_MK): $(GAME)/port/linux/port.json tools/ps3_game_mk.py $(wildcard $(PS3_DIR)/game/*.c)
	@python3 tools/ps3_game_mk.py --game $(GAME) --lp64 $(LP64) --build $(BUILD) --output $@

-include $(GAME_MK)

# rewritten only when the scan finds new tags or inline functions (the originals: tags and names, not types)
$(SEMANTICS): FORCE
	@python3 tools/ps3_msvc_semantics.py --output $@ --tags $(GAME)/source --inlines $(GAME)/source \
		--inlines $(GAME)/port/include/xdk

$(GAME_OBJS): | $(SEMANTICS)
# the XDK declarations are a system folder (-idirafter), which the depfiles leave out
XDK_HEADERS := $(wildcard $(LP64)/port/include/xdk/*.h)
$(GAME_OBJS): $(XDK_HEADERS)

game: $(GAME_OBJS)

# ---------- the Xbox layer: port/ps3/xbox, and the game's platform units that need no host change

PLATFORM_SEMANTICS := $(BUILD)/platform_msvc_semantics.h
# internet play (p2p*.c) and its streams (KCP) have the game's long, as xnet_ps3.c, which calls them.
# xbox_tag_swap.c: the vertices only, after the game's swap (port/console/game)
XBOX_UNITS := $(wildcard $(PS3_DIR)/xbox/*.c) \
              $(addprefix port/linux/src/,bink_null.c d3d8_resources.c halo_linker_common.c msvc_crt.c \
              msvc_wide.c port_config.c xbdm.c xbox_xapi.c p2p.c p2p_copies.c p2p_crypto.c p2p_discord.c \
              p2p_lobby.c p2p_probe.c p2p_signal.c log_address.c thread_stop.c ui_point.c build_identity.c \
              discord_activity.c delta.c delta_compat.c port_folders.c delta_wire.c delta_peer.c delta_chat.c \
              delta_chat_words.c delta_chat_bridge.c delta_peer_game.c) \
              $(KCP_DIR)/ikcp.c
XBOX_OBJS := $(patsubst %.c,$(BUILD)/obj/%.o,$(XBOX_UNITS))

# the game's ABI and the XDK declarations; ps3_platform.h stands in for port/linux/src/platform.h.
# -Wno-format: %ld meets the 32-bit long, which PPC64 passes extended to 64 bits, so it prints right
XBOX_CFLAGS = -std=gnu99 -fgnu89-inline $(GAME_ABI) -mminimal-toc -Wall -Wno-unused-function \
              -Wno-unknown-pragmas -Wno-missing-braces -Wno-unused-variable -Wno-format \
              -Werror=int-to-pointer-cast -Werror=pointer-to-int-cast -Werror=int-conversion \
              -Werror=implicit-function-declaration -DHALO_LINUX_PLATFORM_LAYER $(XBOX_DEFINES) \
              -include $(LP64)/$(PS3_DIR)/include/halo_ps3_prefix.h -include $(PLATFORM_SEMANTICS) \
              -include $(LP64)/$(PS3_DIR)/xbox/ps3_platform.h \
              -I$(LP64)/$(PS3_DIR)/include -I$(LP64)/$(PS3_DIR)/xbox/include -I$(LP64)/port/linux/include \
              -I$(PS3_DIR)/src -I$(PS3_DIR)/spu -I$(LP64)/port/linux/src -I$(LP64)/port/console/include \
              -I$(LP64)/$(TOML_DIR) -I$(LP64)/$(KCP_DIR) -I$(GAME)/port/third_party/monocypher -I$(ZLIB_DIR) \
              -I$(LP64)/source -I$(LP64)/source/cseries -idirafter $(LP64)/port/include/xdk

# internet play's brokers as a C string, which xnet_ps3.c writes beside config.toml at each start
$(BUILD)/brokers_txt.h: $(GAME)/port/assets/network/brokers.txt
	@mkdir -p $(dir $@)
	@sed -e 's/\r$$//' -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/"/' -e 's/$$/\\n"/' $< > $@

$(BUILD)/obj/$(KCP_DIR)/ikcp.o: XBOX_DEFINES := -w
# the signed legacy table over the consoles' HTTPS (src/ps3_online.c fetches it); Delta Peer's build string
$(BUILD)/obj/port/linux/src/delta.o: XBOX_DEFINES := -DHALO_CONSOLE_HTTPS -I$(HTTPS_DIR)
$(BUILD)/obj/port/linux/src/delta_peer_game.o: XBOX_DEFINES := -DHALO_VERSION=\"$(GAME_VERSION)\"
$(BUILD)/obj/$(PS3_DIR)/xbox/xnet_ps3.o: $(BUILD)/brokers_txt.h
$(BUILD)/obj/$(PS3_DIR)/xbox/xnet_ps3.o: XBOX_DEFINES := -I$(BUILD)

# the button icons' RSX texels (EXTRA OPTIONS' BUTTON ICONS), from port/assets/buttons/ps3's PNGs
$(BUILD)/ps3_buttons.h: tools/ps3_button_assets.py $(wildcard port/assets/buttons/ps3/*)
	@mkdir -p $(dir $@)
	@echo "GEN $@"
	@python3 tools/ps3_button_assets.py embed --out $@

$(BUILD)/obj/$(PS3_DIR)/xbox/hud_hires_rsx.o: $(BUILD)/ps3_buttons.h
$(BUILD)/obj/$(PS3_DIR)/xbox/hud_hires_rsx.o: XBOX_DEFINES := -I$(BUILD)

$(PLATFORM_SEMANTICS): FORCE
	@python3 tools/ps3_msvc_semantics.py --output $@ --inlines $(GAME)/port/include/xdk

$(XBOX_OBJS): $(BUILD)/obj/%.o: $(LP64)/%.c $(XDK_HEADERS) | $(PLATFORM_SEMANTICS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(XBOX_CFLAGS) -MMD -MP -c $< -o $@

# ---------- third-party code: the settings file's parser (the game's ABI, as its 64-bit builds), the game's maths
# (port/include/halo_math.h)

TOML_OBJS := $(BUILD)/obj/$(TOML_DIR)/tomlc17.o
MUSL_SRCS := $(wildcard $(MUSL_DIR)/src/*.c)
MUSL_OBJS := $(patsubst $(GAME)/%.c,$(BUILD)/obj/%.o,$(MUSL_SRCS))

$(TOML_OBJS): $(BUILD)/obj/%.o: $(LP64)/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(GAME_ABI) -mminimal-toc -std=gnu11 -Dstatic_assert=_Static_assert -w -I$(LP64)/$(TOML_DIR) \
		-MMD -MP -c $< -o $@

# the same results on every port: no fused multiply-adds
$(MUSL_OBJS): $(BUILD)/obj/%.o: $(GAME)/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) -std=gnu11 -O2 -mcpu=cell -mhard-float -mno-altivec -ffp-contract=off -w -I$(MUSL_DIR)/include \
		-include $(MUSL_DIR)/include/libm.h -MMD -MP -c $< -o $@

# the port's zlib, its inflate only (the game's tools/linux_build.py's ZLIB_SOURCES and ZLIB_DEFINES)
ZLIB_SRCS := $(addprefix $(ZLIB_DIR)/,adler32.c crc32.c inffast.c inflate.c inftrees.c uncompr.c zutil.c)
ZLIB_OBJS := $(patsubst $(GAME)/%.c,$(BUILD)/obj/%.o,$(ZLIB_SRCS))

$(ZLIB_OBJS): $(BUILD)/obj/%.o: $(GAME)/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -std=gnu11 -DZ_PREFIX -Dz_errmsg=z_port_errmsg -w -MMD -MP -c $< -o $@

# Monocypher: internet play's keys and signatures, with the host's long, which its interface has none of
THIRD_PARTY_SRCS := $(GAME)/port/third_party/monocypher/monocypher.c \
                    $(GAME)/port/third_party/monocypher/monocypher-ed25519.c
THIRD_PARTY_OBJS := $(patsubst $(GAME)/%.c,$(BUILD)/obj/%.o,$(THIRD_PARTY_SRCS))

$(THIRD_PARTY_OBJS): $(BUILD)/obj/%.o: $(GAME)/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -std=gnu11 -w -MMD -MP -c $< -o $@

# HTTPS for the game list and the signed legacy table: the game's port/console/https and Mbed TLS 3.6.7 with the
# consoles' configuration and the host's long, their headers before the image's portlibs Mbed TLS 2.28
HTTPS_SRCS  := $(addprefix $(HTTPS_DIR)/,console_https.c console_https_trust.c console_https_roots.c console_pins.c) \
               $(wildcard $(MBEDTLS_DIR)/library/*.c)
HTTPS_OBJS  := $(patsubst $(GAME)/%.c,$(BUILD)/obj/%.o,$(HTTPS_SRCS))
HTTPS_FLAGS := -DMBEDTLS_CONFIG_FILE=\"console_mbedtls_config.h\" -I$(HTTPS_DIR) -I$(MBEDTLS_DIR)/include \
               -I$(MBEDTLS_DIR)/library -I$(GAME)/port/third_party/monocypher

$(HTTPS_OBJS): $(BUILD)/obj/%.o: $(GAME)/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(HTTPS_FLAGS) $(CFLAGS) -std=gnu99 -mminimal-toc -w -MMD -MP -c $< -o $@

$(BUILD)/obj/$(PS3_DIR)/src/ps3_online.o: CFLAGS += -I$(HTTPS_DIR)

# the one unit with VMX (exact, for the simulation: no fused multiply-adds of its own)
$(BUILD)/obj/$(PS3_DIR)/src/ps3_vmx.o: CFLAGS += -maltivec -ffp-contract=off

# ---------- the shader pack: the pixel shader keys of shader_keys.txt and the game's vertex shaders, translated by
# the image's host GCC with the PS3's translator; both without fused multiply-adds, so the same programs

HOST_CC     := gcc
RSX_SRCS    := $(addprefix $(PS3_DIR)/src/,ps3_rsx_asm.c ps3_rsx_vp.c ps3_rsx_fp.c)
PACK_TOOL   := $(BUILD)/tools/ps3_shader_pack
PACK_SRC    := $(BUILD)/gen/ps3_shader_pack.c
PACK_OBJ    := $(BUILD)/obj/gen/ps3_shader_pack.o
SHADER_KEYS := $(PS3_DIR)/shader_keys.txt

$(BUILD)/obj/$(PS3_DIR)/src/ps3_rsx_fp.o $(BUILD)/obj/$(PS3_DIR)/src/ps3_rsx_vp.o: CFLAGS += -ffp-contract=off

$(PACK_TOOL): tools/ps3_shader_pack.c $(RSX_SRCS) $(PS3_DIR)/src/ps3_rsx_program.h
	@mkdir -p $(dir $@)
	@echo "HOSTCC $@"
	@$(HOST_CC) -std=gnu99 -O2 -Wall -ffp-contract=off -I$(PS3_DIR)/src -o $@ tools/ps3_shader_pack.c $(RSX_SRCS) -lm

$(PACK_SRC): $(PACK_TOOL) $(SHADER_KEYS) $(GAME)/source/rasterizer/xbox/rasterizer_xbox_vertex_shaders_data.inc \
	$(GAME)/source/rasterizer/xbox/rasterizer_xbox_vertex_shaders.c
	@mkdir -p $(dir $@)
	@echo "GEN $@"
	@$(PACK_TOOL) --keys $(SHADER_KEYS) --source $(GAME) --out $@

$(PACK_OBJ): $(PACK_SRC)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# SHADER_PACK_CHECK=1: the PS3 translates the pack's keys again at its load and logs the programs that differ (a
# build folder of its own: make does not see a change of flags)
ifeq ($(SHADER_PACK_CHECK),1)
$(BUILD)/obj/$(PS3_DIR)/xbox/d3d8_rsx.o: XBOX_DEFINES := -DPS3_SHADER_PACK_CHECK
endif

# ---------- the SPU programs (port/ps3/spu): spu-gcc, linked in as data (bin2s) for sysSpuImageImport (ps3_spu.c)

SPU_CC     := $(PS3DEV)/spu/bin/spu-gcc
BIN2S      := $(PS3DEV)/bin/bin2s
# no fused multiply-adds: the same float operations as the PPE's mixer (dsound_ps3.c)
SPU_CFLAGS := -std=gnu99 -O2 -Wall -ffp-contract=off -ffunction-sections -fdata-sections \
              -I$(PSL1GHT)/spu/include -I$(PS3_DIR)/spu
SPU_PROGRAMS := spu_mixer spu_vertices spu_game spu_direct3d
SPU_OBJS     := $(patsubst %,$(BUILD)/spu/%.bin.o,$(SPU_PROGRAMS))

$(BUILD)/spu/%.elf: $(PS3_DIR)/spu/%.c $(wildcard $(PS3_DIR)/spu/*.h)
	@mkdir -p $(dir $@)
	@echo "SPU $<"
	@$(SPU_CC) $(SPU_CFLAGS) -Wl,--gc-sections -s $< -L$(PSL1GHT)/spu/lib -lsputhread -o $@

# bin2s names the data after the file: spu_mixer.elf is spu_mixer_elf[]
$(BUILD)/spu/%.bin.o: $(BUILD)/spu/%.elf
	@echo "BIN2S $<"
	@$(BIN2S) -a 64 $< | $(PREFIX)as -o $@

.PRECIOUS: $(BUILD)/spu/%.elf

# ---------- the program

.PHONY: all self pkg check clean game xbox bringup bringup-self symbols FORCE

all: $(BUILD)/$(TARGET).elf

xbox: $(XBOX_OBJS)

# the host objects come first, before libc, so ps3_setjmp.o replaces its setjmp
ALL_OBJS = $(HOST_OBJS) $(XBOX_OBJS) $(TOML_OBJS) $(MUSL_OBJS) $(ZLIB_OBJS) $(THIRD_PARTY_OBJS) $(HTTPS_OBJS) \
           $(SPU_OBJS) $(PACK_OBJ) $(GAME_OBJS)

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
                $(filter-out %/ps3_main.o %/ps3_host.o %/ps3_posix.o %/ps3_gpu.o %/ps3_rsx_asm.o %/ps3_rsx_vp.o \
                %/ps3_rsx_fp.o %/ps3_spu.o %/ps3_online.o %/game_list_parse.o %/game_list_text.o,$(HOST_OBJS))

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

-include $(HOST_OBJS:.o=.d) $(XBOX_OBJS:.o=.d) $(TOML_OBJS:.o=.d) $(MUSL_OBJS:.o=.d) $(ZLIB_OBJS:.o=.d) \
	$(THIRD_PARTY_OBJS:.o=.d) $(HTTPS_OBJS:.o=.d) $(GAME_OBJS:.o=.d) $(BRINGUP_OBJS:.o=.d)
