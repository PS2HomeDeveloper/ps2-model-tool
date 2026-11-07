.DEFAULT_GOAL := all

PYTHON ?= python3
BIN2C ?= bin2c
PS2SDK ?= $(PS2DEV)/ps2sdk
GSKIT ?= $(PS2DEV)/gsKit
RESOURCE_DIR := resources
ASSET_DIR := build/assets
PACK_DIR := build/package
GENERATED_DIR := build/generated
OBJ_DIR := build/obj/ee
BIN_DIR := build/bin
OUTPUT_DIR := build/output
RESOURCE_FILES := $(shell find $(RESOURCE_DIR) -type f -print)
RESOURCE_STAMP := $(ASSET_DIR)/.resources-copied
DATA_PAK := $(PACK_DIR)/DATA.PAK
EMBEDDED_PACK := $(GENERATED_DIR)/DATA.PAK.c
BIN := $(BIN_DIR)/Granny.elf
OUTPUT_ELF := $(OUTPUT_DIR)/usb/Granny.elf
OBJS := $(OBJ_DIR)/main.o $(OBJ_DIR)/core/assets.o $(OBJ_DIR)/core/dbglog.o $(OBJ_DIR)/scene/model/model.o $(OBJ_DIR)/scene/model/model_loader.o $(OBJ_DIR)/scene/model/model_animation.o $(OBJ_DIR)/entities/player/player.o $(OBJ_DIR)/render/renderer.o $(OBJ_DIR)/render/tim2.o $(OBJ_DIR)/render/font_engine.o $(OBJ_DIR)/render/dev_hud.o $(OBJ_DIR)/generated/DATA.PAK.o

EE_INCS := -Iinclude -Isrc -isystem $(GSKIT)/include -isystem $(GSKIT)/include/gs -isystem $(GSKIT)/include/dma -isystem $(GSKIT)/include/toolkit
EE_LDFLAGS := -L$(GSKIT)/lib
EE_CFLAGS := -std=c99 -Wextra -MMD -MP -DGR_EMBEDDED_PACK
EE_LIBS := -lpad -lgskit_toolkit -lgskit -ldmakit -ldebug -lm

.PHONY: all build assets usb clean distclean FORCE

all: $(OUTPUT_ELF)

build: all

assets: $(RESOURCE_STAMP) $(DATA_PAK) $(EMBEDDED_PACK)

usb: $(OUTPUT_ELF)

$(RESOURCE_STAMP): $(RESOURCE_FILES) FORCE
	@rm -rf $(ASSET_DIR)
	@mkdir -p $(ASSET_DIR)
	@cp -a $(RESOURCE_DIR)/. $(ASSET_DIR)/
	@touch $@

$(DATA_PAK): $(RESOURCE_STAMP) tools/pack_resources.py
	@mkdir -p $(@D)
	$(PYTHON) tools/pack_resources.py --resource-dir "$(ASSET_DIR)" --output "$@"

$(EMBEDDED_PACK): $(DATA_PAK)
	@mkdir -p $(@D)
	$(BIN2C) "$<" "$@" gr_pak_blob

SRC_HEADERS := $(shell find src -type f -name '*.h' -print)
INC_HEADERS := $(shell find include -type f -name '*.h' -print)

$(OBJ_DIR)/%.o: src/%.c Makefile $(INC_HEADERS) $(SRC_HEADERS)
	@mkdir -p $(@D)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

$(OBJ_DIR)/generated/DATA.PAK.o: $(EMBEDDED_PACK) Makefile $(INC_HEADERS)
	@mkdir -p $(@D)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

$(BIN): $(OBJS) $(EMBEDDED_PACK)
	@mkdir -p $(@D)
	$(EE_CC) -T$(EE_LINKFILE) $(EE_OPTFLAGS) -o $@ $(OBJS) $(EE_LDFLAGS) $(EXTRA_LDFLAGS) $(EE_LIBS)

$(OUTPUT_ELF): $(BIN)
	@mkdir -p $(@D)
	cp $< $@

clean:
	rm -rf build

distclean: clean

FORCE:

-include $(OBJS:.o=.d)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
