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

SRC_C  := $(wildcard $(SRC)/*.c)
SRC_S  := $(wildcard $(SRC)/*.S)
OBJS   := $(SRC_C:$(SRC)/%.c=$(BUILD)/%.o) $(SRC_S:$(SRC)/%.S=$(BUILD)/%.o)

.PHONY: all iso run clean fetchlet

all: $(BUILD)/$(TARGET)

$(BUILD)/$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(BUILD)/%.o: $(SRC)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.S | $(BUILD)
	$(CC) -m32 -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

iso: $(BUILD)/$(TARGET)
	@mkdir -p $(GRUB)
	@cp $(BUILD)/$(TARGET) $(ISO)/boot/kernel.elf
	@if [ -f grub.cfg ]; then \
	   cp grub.cfg $(GRUB)/grub.cfg; \
	else \
	   echo "No top-level grub.cfg found; leaving existing one in place."; \
	fi
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
	rm -rf $(BUILD) $(ISO)/boot/kernel.elf $(ISO)/oslet.iso
	rm -f $(SRC)/*.o $(BIN)/*.o $(BIN)/*.bin
	find $(ISO)/boot -type d -empty -delete

fetchlet:
	@echo "Building fetchlet.bin..."
	@gcc -m32 -ffreestanding -O2 -nostdlib -fno-pic -fno-pie -fno-stack-protector \
	    -c $(BIN)/fetchlet.c -o $(BIN)/fetchlet.o
	@ld -m elf_i386 -T $(BIN)/fetchlet.ld -nostdlib -o $(BIN)/fetchlet.bin $(BIN)/fetchlet.o
	@rm -f $(BIN)/fetchlet.o
	@echo "Binary created: fetchlet.bin"
