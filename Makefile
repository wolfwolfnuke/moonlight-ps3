# Moonlight PS3 client - build system for the PSL1GHT toolchain.
# See BUILD.md for toolchain + dependency setup.

PS3DEV    ?= /usr/local/ps3dev
PSL1GHT   ?= $(PS3DEV)/psl1ght
PORTLIBS  := $(PS3DEV)/portlibs/ppu

PPU_CC  := $(PS3DEV)/ppu/bin/ppu-gcc
PPU_LD  := $(PS3DEV)/ppu/bin/ppu-gcc
PPU_OBJCOPY := $(PS3DEV)/ppu/bin/ppu-objcopy
MAKE_FSELF := $(PS3DEV)/bin/fself

TARGET := moonlight-ps3
ELF    := $(TARGET).elf
SELF   := $(TARGET).self

# Vendored cross-built dependencies (see BUILD.md).
DEPS := deps

CFLAGS  := -O2 -Wall -Wextra -std=gnu11
CFLAGS  += -I$(PSL1GHT)/ppu/include -I$(PORTLIBS)/include -Isrc
CFLAGS  += -I$(DEPS)/mbedtls/include -I$(DEPS)/libxml2/include/libxml2 -I$(DEPS)/ffmpeg/include
# mbedTLS was built with this custom config; match it when compiling our code.
CFLAGS  += -DMBEDTLS_CONFIG_FILE='"ml_mbedtls_config.h"'

LDFLAGS := -L$(PSL1GHT)/ppu/lib -L$(PORTLIBS)/lib
LDFLAGS += -L$(DEPS)/mbedtls/lib -L$(DEPS)/libxml2/lib -L$(DEPS)/ffmpeg/lib

LIBS := -llv2 -lsysutil -lrsx -lgcm_sys -lnet -lnetctl -lssl -lio -lsysmodule -lrt \
        -laudio -lmbedtls -lmbedx509 -lmbedcrypto -lxml2 -lz -lm

# FFmpeg is enabled only when the cross-built libs are present. The av decoder
# modules (src/av/*.c) compile to no-op stubs otherwise.
# NOTE: cross-building FFmpeg with ppu-gcc 7.2 ICEs on the full allcodecs.c, so
# tools/build_ffmpeg.sh builds a minimal H.264+AAC-only FFmpeg and shrinks the
# registered codec table. See BUILD.md.
FFMPEG_LIB := $(wildcard $(DEPS)/ffmpeg/lib/libavcodec.a)
ifneq ($(FFMPEG_LIB),)
CFLAGS += -DHAVE_FFMPEG
# libm must follow the FFmpeg static libs (they reference llrint/sin/...).
LIBS   += -lavcodec -lavutil -lm
endif

SRC := $(wildcard src/*.c src/*/*.c)
OBJ := $(SRC:.c=.o)

all: $(SELF)

$(ELF): $(OBJ)
	$(PPU_LD) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

$(SELF): $(ELF)
	$(MAKE_FSELF) $< $@

%.o: %.c
	$(PPU_CC) $(CFLAGS) -c $< -o $@

# Build an installable CFW/HEN homebrew package. Requires make_package_npdrm
# from the ps3toolchain (not present in every environment). Stages the .self as
# USRDIR/EBOOT.BIN plus the XMB icon, then invokes the tool.
PKG_DIR := pkg_build
pkg: $(SELF)
	@mkdir -p $(PKG_DIR)/USRDIR
	cp $(SELF) $(PKG_DIR)/USRDIR/EBOOT.BIN
	cp pkg/ICON0.PNG $(PKG_DIR)/ICON0.PNG
	make_package_npdrm pkg/pkg.xml $(PKG_DIR)
	@echo "Created: $(PKG_DIR)/moonlight-ps3.pkg"

clean:
	rm -f $(OBJ) $(ELF) $(SELF)

.PHONY: all clean pkg
