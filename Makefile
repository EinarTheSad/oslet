TARGET  = kernel.elf
BUILD   = build
SRC     = src
ISO     = iso
BIN     = $(SRC)/bin
LIB     = $(SRC)/lib
GRUB    = $(ISO)/boot/grub
LNK     = linker.ld

CC      = gcc
LD      = ld

CFLAGS  = -m32 -ffreestanding -O2 -Wall -Wextra -fno-pic -fno-stack-protector
LDFLAGS = -m elf_i386 -T $(LNK) -nostdlib

# Kernel source directories (excluding lib and bin)
KERNEL_SRC_DIRS := \
	$(SRC) \
	$(SRC)/drivers \
	$(SRC)/mem

# Kernel sources
KERNEL_SRC_C := $(foreach dir,$(KERNEL_SRC_DIRS),$(wildcard $(dir)/*.c))
KERNEL_SRC_S := $(foreach dir,$(KERNEL_SRC_DIRS),$(wildcard $(dir)/*.S))

# Kernel objects (go to build directory)
KERNEL_OBJS := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(KERNEL_SRC_C)) \
               $(patsubst $(SRC)/%.S,$(BUILD)/%.o,$(KERNEL_SRC_S))

.PHONY: all iso run clean binary

all: $(BUILD)/$(TARGET)

$(BUILD)/$(TARGET): $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.S
	@mkdir -p $(dir $@)
	$(CC) -m32 -c $< -o $@

iso: $(BUILD)/$(TARGET)
	@mkdir -p $(ISO)/boot
	@mkdir -p $(GRUB)
	@cp $(BUILD)/$(TARGET) $(ISO)/boot/$(TARGET)
	@if command -v grub-mkrescue >/dev/null 2>&1; then \
	   grub-mkrescue -o $(ISO)/oslet.iso $(ISO); \
	else \
	   echo "grub-mkrescue not found; skipping ISO creation."; \
	fi

run:
	@if [ ! -f "$(ISO)/oslet.iso" ]; then \
		$(MAKE) iso; \
	else \
		echo "ISO already exists, skipping rebuild."; \
	fi
	rm -rf $(BUILD)
	qemu-system-i386 -cdrom $(ISO)/oslet.iso -m 32M -net none -rtc base=localtime \
	    -drive file=disk.img,format=raw,index=0,media=disk \
	    -boot d

clean:
	@echo "Cleaning project..."
	rm -rf $(ISO)/boot/$(TARGET) $(ISO)/oslet.iso
	rm -f $(BIN)/*.bin
	find $(ISO)/boot -type d -empty -delete

# Binary build: one .bin per .c in src/bin, linked with src/lib/*.c
# All C sources for libraries
LIB_SRCS    := $(wildcard $(LIB)/*.c)
LIB_OBJS    := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(LIB_SRCS))

# All C sources for standalone binaries
BIN_SRCS    := $(wildcard $(BIN)/*.c)

# One .bin per bin/*.c
BIN_TARGETS := $(patsubst $(BIN)/%.c,$(BIN)/%.bin,$(BIN_SRCS))

# Use a different linker script for these binaries
binary: LNK := $(BIN)/binary.ld
binary: $(BIN_TARGETS)

$(BIN)/%.bin: $(BUILD)/bin/%.o $(LIB_OBJS)
	@echo "Linking $@..."
	$(LD) $(LDFLAGS) -o $@ $^
	@echo "Binary created: $@"