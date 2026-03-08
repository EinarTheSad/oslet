TARGET  = kernel.elf
BUILD   = build
SRC     = src
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
	$(SRC)/arch \
	$(SRC)/drivers \
	$(SRC)/fonts \
	$(SRC)/irq \
	$(SRC)/mem \
	$(SRC)/task \
	$(SRC)/win

KERNEL_SRC_C := $(foreach dir,$(KERNEL_SRC_DIRS),$(wildcard $(dir)/*.c))
KERNEL_SRC_S := $(foreach dir,$(KERNEL_SRC_DIRS),$(wildcard $(dir)/*.S))

KERNEL_OBJS := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(KERNEL_SRC_C)) \
               $(patsubst $(SRC)/%.S,$(BUILD)/%.o,$(KERNEL_SRC_S))

SHELL_SRC        = $(SRC)/shell
SHELL_OBJ_DIR    = $(BUILD)/obj/shell
SHELL_SRCS       = $(wildcard $(SHELL_SRC)/*.c)
SHELL_OBJS       = $(patsubst $(SHELL_SRC)/%.c,$(SHELL_OBJ_DIR)/%.o,$(SHELL_SRCS))

APPS_AGIX_SRC    = $(SRC)/apps/agix
APPS_AGIX_OBJ_DIR = $(BUILD)/obj/apps/agix
APPS_AGIX_SRCS   = $(wildcard $(APPS_AGIX_SRC)/*.c)
APPS_AGIX_OBJS   = $(patsubst $(APPS_AGIX_SRC)/%.c,$(APPS_AGIX_OBJ_DIR)/%.o,$(APPS_AGIX_SRCS))

APPS_GIX_SRC     = $(SRC)/apps/gix
APPS_GIX_OBJ_DIR = $(BUILD)/obj/apps/gix
APPS_GIX_SRCS    = $(wildcard $(APPS_GIX_SRC)/*.c)
APPS_GIX_OBJS    = $(patsubst $(APPS_GIX_SRC)/%.c,$(APPS_GIX_OBJ_DIR)/%.o,$(APPS_GIX_SRCS))

LIB_SRC          = $(SRC)/lib
LIB_OBJ_DIR      = $(BUILD)/lib
# elf.c contains the ELF‑inspection helper; it is only needed by
# a couple of graphical utilities and should not be pulled into every
# program. filter it out of the general library list and build it on demand later.
LIB_SRCS         = $(filter-out $(LIB_SRC)/elf.c $(LIB_SRC)/mode_marker.c,$(wildcard $(LIB_SRC)/*.c))
LIB_OBJS         = $(patsubst $(LIB_SRC)/%.c,$(LIB_OBJ_DIR)/%.o,$(LIB_SRCS))

# build the ELF helper and two copies of the mode marker
$(LIB_OBJ_DIR)/elf.o: $(LIB_SRC)/elf.c
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -c $< -o $@

$(LIB_OBJ_DIR)/mode_marker_gix.o: $(LIB_SRC)/mode_marker.c
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -DGIX_BUILD -c $< -o $@

$(LIB_OBJ_DIR)/mode_marker_agix.o: $(LIB_SRC)/mode_marker.c
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -DAGIX_BUILD -c $< -o $@

BUILD_BIN        = $(BUILD)/bin

SHELL_TARGETS    = desktop.elf shell.elf
APPS_AGIX_TARGETS = edit.elf fetchlet.elf
APPS_GIX_TARGETS  = calc.elf clock.elf fileman.elf imgview.elf letver.elf terminal.elf

.PHONY: all run clean clean-all disk install binstall desktop fetchlet shell edit terminal clock calc imgview letver fileman binaries full

all: $(BUILD)/$(TARGET)

# Kernel
$(BUILD)/$(TARGET): $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.S
	@mkdir -p $(dir $@)
	$(CC) -m32 -c $< -o $@

# Programs
$(SHELL_OBJ_DIR)/%.o: $(SHELL_SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -c $< -o $@

$(APPS_AGIX_OBJ_DIR)/%.o: $(APPS_AGIX_SRC)/%.c
	@mkdir -p $(dir $@)
	# mark agix binaries so we can detect them at runtime
	$(CC) $(BINCFLAGS) -DAGIX_BUILD -c $< -o $@

$(APPS_GIX_OBJ_DIR)/%.o: $(APPS_GIX_SRC)/%.c
	@mkdir -p $(dir $@)
	# compile graphical programs
	$(CC) $(BINCFLAGS) -DGIX_BUILD -c $< -o $@

# Userland libraries
$(LIB_OBJ_DIR)/%.o: $(LIB_SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -c $< -o $@

# Shells
$(BUILD_BIN)/desktop.elf: $(SHELL_OBJ_DIR)/desktop.o \
						  $(SHELL_OBJ_DIR)/progman.o \
						  $(SHELL_OBJ_DIR)/startman.o \
						  $(SHELL_OBJ_DIR)/textmode.o \
						  $(SHELL_OBJ_DIR)/cpl_theme.o \
						  $(SHELL_OBJ_DIR)/cpl_screen.o \
						  $(SHELL_OBJ_DIR)/cpl_boot.o \
						  $(SHELL_OBJ_DIR)/shutdown.o \
						  $(SHELL_OBJ_DIR)/cpl_volume.o \
						  $(LIB_OBJS) \
						  $(LIB_OBJ_DIR)/elf.o \
						  $(LIB_OBJ_DIR)/mode_marker_agix.o
	@mkdir -p $(BUILD_BIN)
	$(LD) -m elf_i386 -T $(SHELL_SRC)/binary.ld -nostdlib -pie -o $@ $^

$(BUILD_BIN)/shell.elf: $(SHELL_OBJ_DIR)/shell.o $(LIB_OBJS) $(LIB_OBJ_DIR)/elf.o $(LIB_OBJ_DIR)/mode_marker_agix.o
	@mkdir -p $(BUILD_BIN)
	$(LD) -m elf_i386 -T $(SHELL_SRC)/binary.ld -nostdlib -pie -o $@ $^

# Text programs
$(BUILD_BIN)/edit.elf: $(APPS_AGIX_OBJ_DIR)/edit.o $(LIB_OBJS) $(LIB_OBJ_DIR)/mode_marker_agix.o
	@mkdir -p $(BUILD_BIN)
	$(LD) -m elf_i386 -T $(SHELL_SRC)/binary.ld -nostdlib -pie -o $@ $^

$(BUILD_BIN)/fetchlet.elf: $(APPS_AGIX_OBJ_DIR)/fetchlet.o $(LIB_OBJS) $(LIB_OBJ_DIR)/mode_marker_agix.o
	@mkdir -p $(BUILD_BIN)
	$(LD) -m elf_i386 -T $(SHELL_SRC)/binary.ld -nostdlib -pie -o $@ $^

# Graphical programs
$(BUILD_BIN)/calc.elf: $(APPS_GIX_OBJ_DIR)/calc.o $(LIB_OBJS) $(LIB_OBJ_DIR)/mode_marker_gix.o
	@mkdir -p $(BUILD_BIN)
	$(LD) -m elf_i386 -T $(SHELL_SRC)/binary.ld -nostdlib -pie -o $@ $^

$(BUILD_BIN)/clock.elf: $(APPS_GIX_OBJ_DIR)/clock.o $(LIB_OBJS) $(LIB_OBJ_DIR)/mode_marker_gix.o
	@mkdir -p $(BUILD_BIN)
	$(LD) -m elf_i386 -T $(SHELL_SRC)/binary.ld -nostdlib -pie -o $@ $^

$(BUILD_BIN)/fileman.elf: $(APPS_GIX_OBJ_DIR)/fileman.o $(LIB_OBJS) $(LIB_OBJ_DIR)/elf.o $(LIB_OBJ_DIR)/mode_marker_gix.o
	@mkdir -p $(BUILD_BIN)
	$(LD) -m elf_i386 -T $(SHELL_SRC)/binary.ld -nostdlib -pie -o $@ $^

$(BUILD_BIN)/imgview.elf: $(APPS_GIX_OBJ_DIR)/imgview.o $(LIB_OBJS) $(LIB_OBJ_DIR)/mode_marker_gix.o
	@mkdir -p $(BUILD_BIN)
	$(LD) -m elf_i386 -T $(SHELL_SRC)/binary.ld -nostdlib -pie -o $@ $^

$(BUILD_BIN)/letver.elf: $(APPS_GIX_OBJ_DIR)/letver.o $(LIB_OBJS) $(LIB_OBJ_DIR)/mode_marker_gix.o
	@mkdir -p $(BUILD_BIN)
	$(LD) -m elf_i386 -T $(SHELL_SRC)/binary.ld -nostdlib -pie -o $@ $^

$(BUILD_BIN)/terminal.elf: $(APPS_GIX_OBJ_DIR)/terminal.o $(LIB_OBJS) $(LIB_OBJ_DIR)/elf.o $(LIB_OBJ_DIR)/mode_marker_gix.o
	@mkdir -p $(BUILD_BIN)
	$(LD) -m elf_i386 -T $(SHELL_SRC)/binary.ld -nostdlib -pie -o $@ $^

# Quickies for building specific programs
desktop: $(BUILD_BIN)/desktop.elf
fetchlet: $(BUILD_BIN)/fetchlet.elf
shell: $(BUILD_BIN)/shell.elf
edit: $(BUILD_BIN)/edit.elf
terminal: $(BUILD_BIN)/terminal.elf
clock: $(BUILD_BIN)/clock.elf
calc: $(BUILD_BIN)/calc.elf
imgview: $(BUILD_BIN)/imgview.elf
letver: $(BUILD_BIN)/letver.elf
fileman: $(BUILD_BIN)/fileman.elf

# Disk
$(DISK):
	@echo "Creating $(DISK_SIZE)MB disk with MBR..."
	dd if=/dev/zero of=$(DISK) bs=1M count=$(DISK_SIZE)
	echo -e "o\nn\np\n1\n2048\n\nt\nc\na\nw\n" | fdisk $(DISK) || true
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

disk: $(DISK)

# Install kernel and assets
install: $(BUILD)/$(TARGET)
	@if [ ! -f "$(DISK)" ]; then \
		echo "Error: $(DISK) not found. Run 'make disk' first."; \
		exit 1; \
	fi
	@echo "Installing kernel and resources..."
	@LOOP=$$(sudo losetup -f --show -P $(DISK)); \
	mkdir -p mnt; \
	sudo mount $${LOOP}p1 mnt; \
	sudo cp $(BUILD)/$(TARGET) mnt/boot/$(TARGET); \
	sudo mkdir -p mnt/FONTS; \
	sudo cp $(SRC)/assets/bmf/*.[bB][mM][fF] mnt/FONTS/ 2>/dev/null || true; \
	sudo mkdir -p mnt/IMAGES; \
	sudo cp images/*.[bB][mM][pP] mnt/IMAGES/ 2>/dev/null || true; \
	sudo mkdir -p mnt/ICONS; \
	sudo cp $(SRC)/assets/ico/*.[iI][cC][oO] mnt/ICONS/ 2>/dev/null || true; \
	sudo mkdir -p mnt/SOUNDS; \
	sudo cp $(SRC)/assets/wav/*.[wW][aA][vV] mnt/SOUNDS/ 2>/dev/null || true; \
	sudo mkdir -p mnt/OSLET; \
	sudo cp $(SRC)/assets/bmp/*.[bB][mM][pP] mnt/OSLET/ 2>/dev/null || true; \
	sudo umount mnt; \
	sudo losetup -d $$LOOP; \
	rmdir mnt; \
	echo "Kernel and resources installed!"

# Install programs
binstall:
	@if [ ! -f "$(DISK)" ]; then \
		echo "Error: $(DISK) not found. Run 'make disk' first."; \
		exit 1; \
	fi
	@echo "Installing binaries..."
	@LOOP=$$(sudo losetup -f --show -P $(DISK)); \
	mkdir -p mnt; \
	sudo mount $${LOOP}p1 mnt; \
	for f in $(SHELL_TARGETS); do \
		if [ -f $(BUILD_BIN)/$$f ]; then \
			sudo cp $(BUILD_BIN)/$$f mnt/; \
		fi; \
	done; \
	sudo mkdir -p mnt/OSLET/START; \
	for f in $(APPS_AGIX_TARGETS) $(APPS_GIX_TARGETS); do \
		if [ -f $(BUILD_BIN)/$$f ]; then \
			sudo cp $(BUILD_BIN)/$$f mnt/OSLET/START/; \
		fi; \
	done; \
	sudo cp $(SRC)/ini/*.[iI][nN][iI] mnt/OSLET/ 2>/dev/null || true; \
	sudo cp $(SRC)/grp/*.[gG][rR][pP] mnt/OSLET/START/ 2>/dev/null || true; \
	sudo umount mnt; \
	sudo losetup -d $$LOOP; \
	rmdir mnt; \
	echo "Binaries installed!"

binaries: $(BUILD)/$(TARGET) \
         desktop shell edit fetchlet calc clock fileman imgview letver terminal

full: disk binaries install binstall run

# QEMU
run: $(BUILD)/$(TARGET)
	@if [ ! -f "$(DISK)" ]; then \
		$(MAKE) disk; \
	fi
	@$(MAKE) install
	qemu-system-i386 -drive file=$(DISK),format=raw -m 32M -net none -vga std -rtc base=localtime
#-audiodev pa,id=audio0 -device sb16,audiodev=audio0

# Cleaning
clean:
	@echo "Cleaning..."
	rm -rf $(BUILD)

clean-all: clean
	rm -f $(DISK)