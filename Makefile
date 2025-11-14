TARGET  = kernel.elf
BUILD   = build
SRC     = src
ISO     = iso
BIN     = $(SRC)/bin
LIB     = $(SRC)/lib
GRUB    = $(ISO)/boot/grub

CC      = gcc
LD      = ld

CFLAGS  = -m32 -ffreestanding -O2 -Wall -Wextra -fno-pic -fno-stack-protector
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

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

.PHONY: all iso run clean fetchlet

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
	# Compile fetchlet and lib files directly to .o in bin directory
	$(CC) -m32 -ffreestanding -O2 -nostdlib -fno-pic -fno-pie -fno-stack-protector \
	    -c $(BIN)/fetchlet.c -o $(BIN)/fetchlet.o
	$(CC) -m32 -ffreestanding -O2 -nostdlib -fno-pic -fno-pie -fno-stack-protector \
	    -c $(LIB)/stdio.c -o $(BIN)/stdio.o
	$(CC) -m32 -ffreestanding -O2 -nostdlib -fno-pic -fno-pie -fno-stack-protector \
	    -c $(LIB)/string.c -o $(BIN)/string.o
	# Link fetchlet with lib objects
	$(LD) -m elf_i386 -T $(BIN)/fetchlet.ld -nostdlib -o $(BIN)/fetchlet.bin \
	    $(BIN)/fetchlet.o $(BIN)/stdio.o $(BIN)/string.o
	# Clean up object files
	rm -f $(BIN)/*.o
	@echo "Binary created: $(BIN)/fetchlet.bin"