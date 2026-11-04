.DEFAULT_GOAL := all

CC ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra
BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin
TARGET := $(BIN_DIR)/ps2-model-tool
OBJECTS := $(OBJ_DIR)/model_tool.o $(OBJ_DIR)/ufbx.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ -lm

$(OBJ_DIR)/model_tool.o: src/model_tool.c include/gr_limits.h third_party/ufbx/ufbx.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Iinclude -Ithird_party/ufbx -c $< -o $@

$(OBJ_DIR)/ufbx.o: third_party/ufbx/ufbx.c third_party/ufbx/ufbx.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Ithird_party/ufbx -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)
