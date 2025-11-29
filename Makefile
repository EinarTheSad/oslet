TARGET  = kernel.elf
BUILD   = build
SRC     = src
BIN     = $(SRC)/bin
LIB     = $(SRC)/lib
LNK     = linker.ld
DISK    = disk.img
DISK_SIZE = 32

CC      = gcc
LD      = ld

CFLAGS  = -m32 -ffreestanding -O2 -Wall -Wextra -fno-pic -fno-pie -fno-stack-protector
BINCFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fPIE -fno-plt
LDFLAGS = -m elf_i386 -T $(LNK) -nostdlib

KERNEL_SRC_DIRS := \
	$(SRC) \
	$(SRC)/drivers \
	$(SRC)/mem \
	$(SRC)/fonts

KERNEL_SRC_C := $(foreach dir,$(KERNEL_SRC_DIRS),$(wildcard $(dir)/*.c))
KERNEL_SRC_S := $(foreach dir,$(KERNEL_SRC_DIRS),$(wildcard $(dir)/*.S))

KERNEL_OBJS := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(KERNEL_SRC_C)) \
               $(patsubst $(SRC)/%.S,$(BUILD)/%.o,$(KERNEL_SRC_S))

.PHONY: all iso run clean disk install fetchlet shell demo

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
	@echo "Creating $(DISK_SIZE)MB disk with MBR..."
	dd if=/dev/zero of=$(DISK) bs=1M count=$(DISK_SIZE)
	echo -e "o\nn\np\n1\n2048\n\nt\nc\na\nw\n" | fdisk $(DISK)
	@echo "Setting up loop device..."
	@LOOP=$$(sudo losetup -f --show -P $(DISK)); \
	echo "Loop: $$LOOP"; \
	sleep 1; \
	echo "Formatting partition..."; \
	sudo mkfs.vfat -F 32 -n BOOTPART $${LOOP}p1; \
	echo "Mounting..."; \
	mkdir -p mnt; \
	sudo mount $${LOOP}p1 mnt; \
	echo "Creating directories..."; \
	sudo mkdir -p mnt/boot/grub; \
	echo "Copying GRUB config..."; \
	sudo cp grub.cfg mnt/boot/grub/grub.cfg; \
	echo "Installing GRUB..."; \
	sudo grub-install --target=i386-pc --boot-directory=mnt/boot --force $$LOOP; \
	echo "Unmounting..."; \
	sudo umount mnt; \
	sudo losetup -d $$LOOP; \
	rmdir mnt; \
	echo "Disk ready!"

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
	sudo mkdir -p mnt/FONTS; \
	sudo cp src/fonts/*.bmf mnt/FONTS/; \
	sudo cp *.bmp mnt/; \
	sudo umount mnt; \
	sudo losetup -d $$LOOP; \
	rmdir mnt; \
	echo "Kernel installed!"

disk: $(DISK)

iso: $(BUILD)/$(TARGET)
	@mkdir -p mnt/boot/grub
	@cp $(BUILD)/$(TARGET) mnt/boot/$(TARGET)
	@cp grub.cfg mnt/boot/grub
	@if command -v grub-mkrescue >/dev/null 2>&1; then \
		grub-mkrescue -o oslet.iso mnt; \
	else \
		echo "grub-mkrescue not found; skipping ISO creation."; \
	fi
	@rm -rf mnt

run: $(BUILD)/$(TARGET)
	@if [ ! -f "$(DISK)" ]; then \
		$(MAKE) disk; \
	fi
	@$(MAKE) install
	qemu-system-i386 -drive file=$(DISK),format=raw -m 32M -net none -vga std -rtc base=localtime

clean:
	@echo "Cleaning..."
	rm -rf $(BUILD)
	rm -f $(BIN)/*.elf

clean-all: clean
	rm -f $(DISK)
	rm -f oslet.iso

# Binaries
LIB_SRCS := $(wildcard $(LIB)/*.c)
LIB_OBJS := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(LIB_SRCS))

binstall:
	@if [ ! -f "$(DISK)" ]; then \
		echo "Error: $(DISK) not found. Run 'make disk' first."; \
		exit 1; \
	fi
	@echo "Installing binaries..."
	@LOOP=$$(sudo losetup -f --show -P $(DISK)); \
	mkdir -p mnt; \
	sudo mount $${LOOP}p1 mnt; \
	sudo cp $(BIN)/*.elf mnt/; \
	sudo umount mnt; \
	sudo losetup -d $$LOOP; \
	rmdir mnt
	@echo "Binaries installed!"

fetchlet: $(BIN)/fetchlet.elf

$(BIN)/fetchlet.elf: $(BUILD)/bin/fetchlet.o $(LIB_OBJS)
	$(LD) -m elf_i386 -T $(BIN)/binary.ld -nostdlib -pie -o $@ $^

$(BUILD)/bin/%.o: $(BIN)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -c $< -o $@

$(BUILD)/lib/%.o: $(LIB)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -c $< -o $@

shell: $(BIN)/shell.elf

$(BIN)/shell.elf: $(BUILD)/bin/shell.o $(LIB_OBJS)
	$(LD) -m elf_i386 -T $(BIN)/binary.ld -nostdlib -pie -o $@ $^

$(BUILD)/bin/%.o: $(BIN)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -c $< -o $@

$(BUILD)/lib/%.o: $(LIB)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -c $< -o $@

demo: $(BIN)/demo.elf

$(BIN)/demo.elf: $(BUILD)/bin/demo.o $(LIB_OBJS)
	$(LD) -m elf_i386 -T $(BIN)/binary.ld -nostdlib -pie -o $@ $^

$(BUILD)/bin/%.o: $(BIN)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -c $< -o $@

$(BUILD)/lib/%.o: $(LIB)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -c $< -o $@