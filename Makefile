TARGET_NAME := froggysm

SRC_DIR := src
INC_DIR := include

SOURCES_C := \
	$(SRC_DIR)/libretro.c \
	$(SRC_DIR)/sm_timing.c \
	$(SRC_DIR)/sm_song.c \
	$(SRC_DIR)/sm_library.c \
	$(SRC_DIR)/sm_game.c \
	$(SRC_DIR)/sm_render.c \
	$(SRC_DIR)/sm_audio.c \
	$(SRC_DIR)/sm_bg.c \
	$(SRC_DIR)/sm_theme.c \
	$(SRC_DIR)/sm_assets.c

OBJS := $(SOURCES_C:.c=.o)

SONGS_PATH ?= /home/Sajnaps/pspm-songs
RUNNER     := runner/test_runner

# Path to a checkout of https://github.com/Synaps33/froggysm-multicore
# (the sf2000_multicore frontend that wraps the libretro core into a
# core_87000000 payload for the GB300 / SF2000 firmware).
MULTICORE_DIR ?= /home/Sajnaps/gb300/sf2000_multicore

# Which firmware the payload is linked for.  The game code is identical either
# way; only the BIOS syscall addresses and the LCD MADCTL value differ.
FROGGY_TYPE ?= SF2000

ifeq ($(platform), sf2000)
	MIPS := /opt/frog-toolchain/mipsel-mti-elf_stable/bin/mipsel-mti-elf-
	CC := $(MIPS)gcc
	AR := $(MIPS)ar
	RANLIB := $(MIPS)ranlib

	# The GB300/SF2000 core links against -lc only: no libm on the target, so
	# every transcendental the engine needs is implemented in-tree.
	CFLAGS := -EL -march=mips32 -mtune=mips32r2 -msoft-float -O2
	CFLAGS += -G0 -mno-abicalls -fno-pic
	CFLAGS += -ffunction-sections -fdata-sections
	CFLAGS += -DSM_DEVICE -DSDCARD_DIRECTORY='"/mnt/sda1"'
	CFLAGS += -Wall -I$(INC_DIR) -I$(SRC_DIR)
	CFLAGS += -I$(MULTICORE_DIR)/include
	TARGET := $(TARGET_NAME)_libretro_sf2000.a

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) rcs $@ $(OBJS)
	$(RANLIB) $@

else
	CC ?= gcc
	CFLAGS := -O2 -Wall -Wextra -I$(INC_DIR) -I$(SRC_DIR)
	TARGET_SO := $(TARGET_NAME)_libretro.so

all: $(TARGET_SO) $(RUNNER)

$(TARGET_SO): $(OBJS)
	$(CC) -shared -fPIC -o $@ $(OBJS) -lm

$(RUNNER): runner/test_runner.c $(SOURCES_C)
	$(CC) $(CFLAGS) -o $@ runner/test_runner.c $(SOURCES_C) -lm

test: $(RUNNER)
	SM_SONGS_PATH=$(SONGS_PATH) ./$(RUNNER)

endif

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(SRC_DIR)/*.o $(TARGET) $(TARGET_SO) $(RUNNER)
	rm -f *_libretro*.a
	rm -rf dist

device: $(SONGS_PATH)
	$(MAKE) platform=sf2000
	$(MAKE) -C $(MULTICORE_DIR) clean
	mkdir -p $(MULTICORE_DIR)/build
	$(MAKE) -C $(MULTICORE_DIR) CORE=cores/froggysm FROGGY_TYPE=$(FROGGY_TYPE)
	mkdir -p ./dist
	cp -f $(MULTICORE_DIR)/build/core_87000000 ./dist/froggysm.sf2k
	@echo "FroggySM core built successfully -> dist/froggysm.sf2k"

$(SONGS_PATH):
	mkdir -p $(SONGS_PATH)
	cp -r /tmp/opencode/pspm/PspManiaV4/Songs/. $(SONGS_PATH)/

.PHONY: all test clean device