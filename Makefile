TARGET  = kernel.elf
BUILD   = build
SRC     = src
ISO     = iso
BIN     = $(SRC)/bin
GRUB    = $(ISO)/boot/grub

CC      = gcc
LD      = ld

CFLAGS  = -m32 -ffreestanding -O2 -Wall -Wextra -fno-pic -fno-stack-protector
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

SRC_DIRS := \
	$(SRC) \
	$(SRC)/drivers \
	$(SRC)/mem

SRC_C := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
SRC_S := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.S))

OBJS := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(SRC_C)) \
        $(patsubst $(SRC)/%.S,$(BUILD)/%.o,$(SRC_S))

.PHONY: all iso run clean fetchlet

all: $(BUILD)/$(TARGET)

$(BUILD)/$(TARGET): $(OBJS)
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
	# grub.cfg is already tracked in iso/boot/grub/grub.cfg according to your tree,
	# so we just leave it there and don't try to copy from top level.
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
	qemu-system-i386 -cdrom $(ISO)/oslet.iso -m 512M -net none -rtc base=localtime \
	    -drive file=disk.img,format=raw,index=0,media=disk \
	    -boot d

clean:
	@echo "Cleaning project..."
	rm -rf $(BUILD) $(ISO)/boot/$(TARGET) $(ISO)/oslet.iso
	rm -f $(BIN)/*.o $(BIN)/*.bin
	find $(ISO)/boot -type d -empty -delete

fetchlet:
	@echo "Building fetchlet.bin..."
	$(CC) -m32 -ffreestanding -O2 -nostdlib -fno-pic -fno-pie -fno-stack-protector \
	    -c $(BIN)/fetchlet.c -o $(BIN)/fetchlet.o
	$(LD) -m elf_i386 -T $(BIN)/fetchlet.ld -nostdlib -o $(BIN)/fetchlet.bin $(BIN)/fetchlet.o
	rm -f $(BIN)/fetchlet.o
	@echo "Binary created: $(BIN)/fetchlet.bin"