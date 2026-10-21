# Super Mario Bros. on the NumWorks calculator: Nintendo's game run natively by the SMB-Vanilla engine
# (vendor/smbcore, GPL-3.0), with the calculator's screen, keys and saves (src/).
#   make          output/SuperMarioBros.nwa
#   make check    link it like the calculator does, with the 133652 bytes of RAM custom builds of Epsilon give apps
#   make test     the game on a computer: every level, saves, the reference run and the fidelity fingerprint
#   make assets   src/data.c, src/rom.h and src/icon.png again from assets/ (Python + Pillow)
Q ?= @
# the ARM toolchain on the PATH, or in TOOLCHAIN=<its bin folder>
TOOLCHAIN ?=
BIN = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)
CC = $(BIN)arm-none-eabi-gcc
STRIP = $(BIN)arm-none-eabi-strip --strip-unneeded
SIZE = $(BIN)arm-none-eabi-size
NWLINK ?= npx --yes -- nwlink@1.0.0
PYTHON ?= python3
HOST_CC ?= cc
OUT = output
APP = $(OUT)/SuperMarioBros.nwa
RAM = 133652

SRC = src/main.c src/game.c src/render.c src/data.c
CORE = vendor/smbcore/common.c vendor/smbcore/common_sound.c vendor/smbcore/area.c vendor/smbcore/smb1only.c
OBJ = $(patsubst %.c,$(OUT)/obj/%.o,$(SRC) $(CORE))
HEADERS = $(wildcard src/*.h vendor/smbcore/*.h common/*.h)

# The engine needs these on every compiler: ARM's char is unsigned by default, which would change the
# original loop-command parser.
GAME_FLAGS = -std=c11 -DSMB1_MODE -fwrapv -fsigned-char -fno-strict-aliasing
CFLAGS = $(GAME_FLAGS) $(shell $(NWLINK) eadk-cflags-device) -Os -DNDEBUG -Wall -Wextra -Werror
CFLAGS += -Wno-unused-parameter -Wno-unused-variable -ffunction-sections -fdata-sections
CFLAGS += -fno-tree-loop-distribute-patterns -flto -fno-fat-lto-objects -fvisibility=internal
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs -flinker-output=nolto-rel
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level -Wl,--gc-sections

.PHONY: build check test assets clean
build: $(APP)

$(OUT)/obj/%.o: %.c $(HEADERS)
	@mkdir -p $(@D)
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

$(OUT)/icon.o: src/icon.png
	@mkdir -p $(@D)
	$(Q) $(NWLINK) png-icon-o $< $@

$(APP): $(OBJ) $(OUT)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $^ -lm -o $@
	$(Q) $(STRIP) $@
	$(Q) $(SIZE) $@

check: $(APP)
	$(Q) $(NWLINK) nwa-bin --ram-length $(RAM) $< $(OUT)/SuperMarioBros.bin
	@echo "BIN     $(OUT)/SuperMarioBros.bin: $$(wc -c < $(OUT)/SuperMarioBros.bin | tr -d " ") bytes, fits $(RAM) bytes of RAM"

HOST = $(HOST_CC) $(GAME_FLAGS) -O2
test:
	@mkdir -p $(OUT)
	$(Q) $(HOST) tests/test.c src/game.c src/render.c src/data.c $(CORE) -o $(OUT)/test
	$(Q) cd $(OUT) && ./test
	$(Q) $(HOST) tests/rom.c src/game.c src/data.c $(CORE) -o $(OUT)/rom
	$(Q) $(OUT)/rom assets/tables.bin tests/legacy-save.bin
	$(Q) $(HOST) tests/replay.c src/game.c src/data.c $(CORE) -o $(OUT)/replay
	$(Q) $(OUT)/replay tests/reference.rec
	$(Q) $(HOST) tests/fidelity.c src/game.c src/render.c src/data.c $(CORE) -o $(OUT)/fidelity
	$(Q) cd $(OUT) && ./fidelity ../tests/reference.rec ../tests/fidelity.expected
	$(Q) $(PYTHON) tools/assets.py --check

assets:
	$(PYTHON) tools/assets.py

clean:
	rm -rf $(OUT)
