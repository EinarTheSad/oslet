TARGET  = kernel.elf
BUILD   = build
SRC     = src
BIN     = $(SRC)/bin
LIB     = $(SRC)/lib
LNK     = linker.ld
MNT		= mnt
DISK    = disk.img
DISK_SIZE = 16

CC      = gcc
LD      = ld

CFLAGS  = -m32 -ffreestanding -O2 -Wall -Wextra -fno-pic -fno-pie -fno-stack-protector
LDFLAGS = -m elf_i386 -T $(LNK) -nostdlib

KERNEL_SRC_DIRS := \
	$(SRC) \
	$(SRC)/drivers \
	$(SRC)/mem

KERNEL_SRC_C := $(foreach dir,$(KERNEL_SRC_DIRS),$(wildcard $(dir)/*.c))
KERNEL_SRC_S := $(foreach dir,$(KERNEL_SRC_DIRS),$(wildcard $(dir)/*.S))

KERNEL_OBJS := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(KERNEL_SRC_C)) \
               $(patsubst $(SRC)/%.S,$(BUILD)/%.o,$(KERNEL_SRC_S))

.PHONY: all run clean disk install fetchlet

all: $(BUILD)/$(TARGET)

$(BUILD)/$(TARGET): $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.S
	@mkdir -p $(dir $@)
	$(CC) -m32 -c $< -o $@

$(DISK):
	@echo "Creating disk image..."
	dd if=/dev/zero of=$(DISK) bs=1M count=$(DISK_SIZE)
	@echo "Setting up loop device..."
	sudo losetup -Pf $(DISK)
	LOOP=$$(losetup -a | grep "$(DISK)" | cut -d: -f1); \
	echo "Loop: $$LOOP"; \
	echo "Creating MBR table..."; \
	sudo parted $$LOOP mklabel msdos; \
	echo "Creating FAT32 partition..."; \
	sudo parted $$LOOP mkpart primary fat32 1MiB 100%; \
	echo "Formatting..."; \
	sudo mkfs.vfat -F 32 $${LOOP}p1; \
	echo "Mounting..."; \
	mkdir -p $(MNT); \
	sudo mount $${LOOP}p1 $(MNT); \
	echo "Creating catalogues..."; \
	sudo mkdir -p $(MNT)/boot/grub; \
	echo "Copying grub.cfg..."; \
	sudo cp grub.cfg $(MNT)/boot/grub/grub.cfg; \
	echo "Installing GRUB..."; \
	sudo grub-install --target=i386-pc --boot-directory=$(MNT)/boot $$LOOP; \
	echo "Unmounting..."; \
	sudo umount $(MNT); \
	rmdir $(MNT); \
	echo "Disengaging loop..."; \
	sudo losetup -d $$LOOP
	@echo "Ready to run `make install`"

install: $(BUILD)/$(TARGET)
	@if [ ! -f "$(DISK)" ]; then \
		echo "Error: $(DISK) not found. Run 'make disk' first."; \
		exit 1; \
	fi
	@echo "Installing kernel..."
	@LOOP=$$(sudo losetup -f --show -P $(DISK)); \
	mkdir -p mnt; \
	sudo mount $${LOOP}p1 mnt; \
	sudo cp $(BUILD)/$(TARGET) mnt/boot/$(TARGET); \
	sudo umount mnt; \
	sudo losetup -d $$LOOP; \
	rmdir mnt
	@echo "Kernel installed!"

disk: $(DISK)

run: $(BUILD)/$(TARGET)
	@if [ ! -f "$(DISK)" ]; then \
		$(MAKE) disk; \
	fi
	@$(MAKE) install
	qemu-system-i386 -drive file=$(DISK),format=raw -m 32M -net none -rtc base=localtime

clean:
	@echo "Cleaning..."
	rm -rf $(BUILD)
	rm -f $(BIN)/*.bin

clean-all: clean
	rm -f $(DISK)

# Binaries
LIB_SRCS := $(wildcard $(LIB)/*.c)
LIB_OBJS := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(LIB_SRCS))

fetchlet: $(BIN)/fetchlet.bin

$(BIN)/fetchlet.bin: $(BUILD)/bin/fetchlet.o $(LIB_OBJS)
	$(LD) -m elf_i386 -T $(BIN)/binary.ld -nostdlib -o $@ $^

$(BUILD)/bin/%.o: $(BIN)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/lib/%.o: $(LIB)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
