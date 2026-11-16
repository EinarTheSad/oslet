TARGET  = kernel.elf
BUILD   = build
SRC     = src
LNK     = linker.ld
DISK    = disk.img
DISK_SIZE = 32

CC      = gcc
LD      = ld
QEMU_AUDIO ?= sdl
QEMU_DISPLAY ?= sdl,grab-mod=lctrl-lalt,show-cursor=on
QEMU_SOUND ?= -audiodev $(QEMU_AUDIO),id=snd0 -device sb16,audiodev=snd0

CFLAGS  = -m32 -ffreestanding -O2 -Wall -Wextra -fno-pic -fno-pie -fno-stack-protector -MMD -MP
BINCFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fPIE -fno-plt -ffunction-sections -fdata-sections -MMD -MP
LDFLAGS = -m elf_i386 -T $(LNK) -nostdlib
BIN_LDFLAGS = -m elf_i386 -T $(SRC)/shell/binary.ld -nostdlib -pie --gc-sections

KERNEL_SUBDIRS = arch drivers fonts irq mem task win

KERNEL_ROOT_OBJS = $(CURDIR)/$(BUILD)/kernel.o \
                  $(CURDIR)/$(BUILD)/console.o \
                  $(CURDIR)/$(BUILD)/vconsole.o \
                  $(CURDIR)/$(BUILD)/rtc.o \
                  $(CURDIR)/$(BUILD)/shell_kernel.o

KERNEL_SYSCALL_OBJS = $(CURDIR)/$(BUILD)/syscall/core.o \
                      $(CURDIR)/$(BUILD)/syscall/dropdown.o \
                      $(CURDIR)/$(BUILD)/syscall/gui.o \
                      $(CURDIR)/$(BUILD)/syscall/gui_events.o \
                      $(CURDIR)/$(BUILD)/syscall/gui_helpers.o \
                      $(CURDIR)/$(BUILD)/syscall/lists.o \
                      $(CURDIR)/$(BUILD)/syscall/press.o \
                      $(CURDIR)/$(BUILD)/syscall/scroll.o \
                      $(CURDIR)/$(BUILD)/syscall/textbox.o

KERNEL_SUBDIR_SRCS_C := $(foreach dir,$(KERNEL_SUBDIRS),$(wildcard $(SRC)/$(dir)/*.c)) \
                        $(wildcard $(SRC)/drivers/fat32/*.c) \
                        $(wildcard $(SRC)/drivers/graphics/*.c)
KERNEL_WIN_CONTROL_SRCS_C := $(wildcard $(SRC)/win/controls/*.c)
KERNEL_SUBDIR_SRCS_S := $(foreach dir,$(KERNEL_SUBDIRS),$(wildcard $(SRC)/$(dir)/*.S))
KERNEL_SUBDIR_OBJS := $(patsubst $(SRC)/%.c,$(CURDIR)/$(BUILD)/%.o,$(KERNEL_SUBDIR_SRCS_C)) \
                      $(patsubst $(SRC)/%.c,$(CURDIR)/$(BUILD)/%.o,$(KERNEL_WIN_CONTROL_SRCS_C)) \
                      $(patsubst $(SRC)/%.S,$(CURDIR)/$(BUILD)/%.o,$(KERNEL_SUBDIR_SRCS_S))

KERNEL_OBJS = $(KERNEL_ROOT_OBJS) $(KERNEL_SYSCALL_OBJS) $(KERNEL_SUBDIR_OBJS)

LIB_OBJ_DIR      = $(CURDIR)/$(BUILD)/lib
LIB_SRCS         = $(filter-out $(SRC)/lib/elf.c $(SRC)/lib/mode_marker.c,$(wildcard $(SRC)/lib/*.c))
LIB_OBJS         = $(patsubst $(SRC)/lib/%.c,$(LIB_OBJ_DIR)/%.o,$(LIB_SRCS))
BINARY_CONFIG_STAMP = $(CURDIR)/$(BUILD)/.userland-config

BUILD_BIN        = $(CURDIR)/$(BUILD)/bin

SHELL_OBJ_DIR    = $(CURDIR)/$(BUILD)/obj/shell
APPS_AGIX_OBJ_DIR = $(CURDIR)/$(BUILD)/obj/apps/agix
APPS_GIX_OBJ_DIR = $(CURDIR)/$(BUILD)/obj/apps/gix

SHELL_SRCS       := $(filter-out $(SRC)/shell/cpl_%.c $(SRC)/shell/progman.c $(SRC)/shell/startman.c $(SRC)/shell/textmode.c $(SRC)/shell/shutdown.c,$(wildcard $(SRC)/shell/*.c))
SHELL_TARGETS     := $(patsubst $(SRC)/shell/%.c,%.elf,$(SHELL_SRCS))
SHELL_OBJS        := $(patsubst $(SRC)/shell/%.c,$(SHELL_OBJ_DIR)/%.o,$(wildcard $(SRC)/shell/*.c))

AGIX_SRCS        := $(wildcard $(SRC)/apps/agix/*.c)
AGIX_TARGETS     := $(patsubst $(SRC)/apps/agix/%.c,%.elf,$(AGIX_SRCS))
AGIX_OBJS        := $(patsubst $(SRC)/apps/agix/%.c,$(APPS_AGIX_OBJ_DIR)/%.o,$(AGIX_SRCS))

GIX_SRCS         := $(wildcard $(SRC)/apps/gix/*.c)
GIX_TARGETS      := $(patsubst $(SRC)/apps/gix/%.c,%.elf,$(GIX_SRCS))
GIX_OBJS         := $(patsubst $(SRC)/apps/gix/%.c,$(APPS_GIX_OBJ_DIR)/%.o,$(GIX_SRCS))

PROGRAM_NAMES    := $(foreach t,$(SHELL_TARGETS) $(AGIX_TARGETS) $(GIX_TARGETS),$(basename $(t)))
PROGRAM_BINARIES := $(addprefix $(BUILD_BIN)/,$(SHELL_TARGETS) $(AGIX_TARGETS) $(GIX_TARGETS))
DEP_FILES        := $(KERNEL_OBJS:.o=.d) $(LIB_OBJS:.o=.d) \
                    $(LIB_OBJ_DIR)/elf.d $(LIB_OBJ_DIR)/mode_marker_gix.d \
                    $(LIB_OBJ_DIR)/mode_marker_agix.d $(SHELL_OBJS:.o=.d) \
                    $(AGIX_OBJS:.o=.d) $(GIX_OBJS:.o=.d)

.PHONY: all kernel run run-ahci run-usb clean clean-all disk fresh-disk install binstall binaries update full programs FORCE $(PROGRAM_NAMES)

all: $(CURDIR)/$(BUILD)/$(TARGET)

kernel: $(CURDIR)/$(BUILD)/$(TARGET)

$(CURDIR)/$(BUILD)/$(TARGET): $(KERNEL_OBJS) $(LNK)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

$(CURDIR)/$(BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(CURDIR)/$(BUILD)/%.o: $(SRC)/%.S
	@mkdir -p $(dir $@)
	$(CC) -m32 -MMD -MP -c $< -o $@

FORCE:

$(BINARY_CONFIG_STAMP): FORCE
	@mkdir -p $(dir $@)
	@if ! printf '%s\n' '$(BINCFLAGS) $(BIN_LDFLAGS)' | cmp -s - $@; then \
		printf '%s\n' '$(BINCFLAGS) $(BIN_LDFLAGS)' > $@; \
	fi

$(CURDIR)/$(BUILD)/obj/shell/%.o: $(SRC)/shell/%.c $(BINARY_CONFIG_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -c $< -o $@

$(LIB_OBJ_DIR)/%.o: $(SRC)/lib/%.c $(BINARY_CONFIG_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -c $< -o $@

$(LIB_OBJ_DIR)/mode_marker_gix.o: $(SRC)/lib/mode_marker.c $(BINARY_CONFIG_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -DGIX_BUILD -c $< -o $@

$(LIB_OBJ_DIR)/mode_marker_agix.o: $(SRC)/lib/mode_marker.c $(BINARY_CONFIG_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -DAGIX_BUILD -c $< -o $@

$(APPS_AGIX_OBJ_DIR)/%.o: $(SRC)/apps/agix/%.c $(BINARY_CONFIG_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -DAGIX_BUILD -c $< -o $@

$(APPS_GIX_OBJ_DIR)/%.o: $(SRC)/apps/gix/%.c $(BINARY_CONFIG_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -DGIX_BUILD -c $< -o $@

programs: $(LIB_OBJS) $(LIB_OBJ_DIR)/elf.o \
          $(LIB_OBJ_DIR)/mode_marker_gix.o $(LIB_OBJ_DIR)/mode_marker_agix.o \
          $(SHELL_OBJS) $(AGIX_OBJS) $(GIX_OBJS)

$(BUILD_BIN)/desktop.elf: $(SHELL_OBJ_DIR)/desktop.o \
					  $(SHELL_OBJ_DIR)/progman.o \
					  $(SHELL_OBJ_DIR)/startman.o \
					  $(SHELL_OBJ_DIR)/textmode.o \
					  $(SHELL_OBJ_DIR)/cpl_theme.o \
					  $(SHELL_OBJ_DIR)/cpl_screen.o \
					  $(SHELL_OBJ_DIR)/cpl_boot.o \
					  $(SHELL_OBJ_DIR)/cpl_palette.o \
					  $(SHELL_OBJ_DIR)/shutdown.o \
					  $(SHELL_OBJ_DIR)/cpl_volume.o \
					  $(LIB_OBJS) \
					  $(LIB_OBJ_DIR)/elf.o \
					  $(LIB_OBJ_DIR)/mode_marker_agix.o \
					  $(BINARY_CONFIG_STAMP) \
					  $(SRC)/shell/binary.ld
	@mkdir -p $(BUILD_BIN)
	$(LD) $(BIN_LDFLAGS) -o $@ $(filter %.o,$^)

$(BUILD_BIN)/shell.elf: $(SHELL_OBJ_DIR)/shell.o $(LIB_OBJS) $(LIB_OBJ_DIR)/elf.o $(LIB_OBJ_DIR)/mode_marker_agix.o $(BINARY_CONFIG_STAMP) $(SRC)/shell/binary.ld
	@mkdir -p $(BUILD_BIN)
	$(LD) $(BIN_LDFLAGS) -o $@ $(filter %.o,$^)

GIX_TARGETS_NOEXT := $(patsubst $(SRC)/apps/gix/%.c,%,$(GIX_SRCS))
AGIX_TARGETS_NOEXT := $(patsubst $(SRC)/apps/agix/%.c,%,$(AGIX_SRCS))

$(GIX_TARGETS_NOEXT:%=$(BUILD_BIN)/%.elf): $(BUILD_BIN)/%.elf: $(APPS_GIX_OBJ_DIR)/%.o $(LIB_OBJS) $(LIB_OBJ_DIR)/elf.o $(LIB_OBJ_DIR)/mode_marker_gix.o $(BINARY_CONFIG_STAMP) $(SRC)/shell/binary.ld
	@mkdir -p $(BUILD_BIN)
	$(LD) $(BIN_LDFLAGS) -o $@ $(filter %.o,$^)

$(AGIX_TARGETS_NOEXT:%=$(BUILD_BIN)/%.elf): $(BUILD_BIN)/%.elf: $(APPS_AGIX_OBJ_DIR)/%.o $(LIB_OBJS) $(LIB_OBJ_DIR)/mode_marker_agix.o $(BINARY_CONFIG_STAMP) $(SRC)/shell/binary.ld
	@mkdir -p $(BUILD_BIN)
	$(LD) $(BIN_LDFLAGS) -o $@ $(filter %.o,$^)

binaries: $(PROGRAM_BINARIES)

$(PROGRAM_NAMES): %: $(BUILD_BIN)/%.elf

$(DISK):
	@echo "WARNING: creating a fresh $(DISK); any existing image data will be replaced."
	@echo "Creating $(DISK_SIZE)MB disk with MBR..."
	dd if=/dev/zero of=$(DISK) bs=1M count=$(DISK_SIZE)
	echo -e "o\nn\np\n1\n2048\n\nt\nc\na\nw\n" | fdisk $(DISK) || true
	@echo "Setting up loop device..."
	@set -e; \
	LOOP=""; \
	cleanup() { status=$$?; sudo umount mnt 2>/dev/null || true; if [ -n "$$LOOP" ]; then sudo losetup -d $$LOOP 2>/dev/null || true; fi; rmdir mnt 2>/dev/null || true; exit $$status; }; \
	trap cleanup EXIT; \
	LOOP=$$(sudo losetup -f --show -P $(DISK)); \
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
	LOOP=""; \
	rmdir mnt; \
	trap - EXIT; \
	echo "Disk ready!"

disk: $(DISK)

fresh-disk:
	@echo "WARNING: removing and recreating $(DISK). Use 'make update' to preserve an existing image."
	@rm -f $(DISK)
	@$(MAKE) disk

install: $(CURDIR)/$(BUILD)/$(TARGET)
	@if [ ! -f "$(DISK)" ]; then \
		echo "Error: $(DISK) not found. Run 'make disk' first."; \
		exit 1; \
	fi
	@echo "Installing kernel and resources..."
	@set -e; \
	LOOP=""; \
	cleanup() { status=$$?; sudo umount mnt 2>/dev/null || true; if [ -n "$$LOOP" ]; then sudo losetup -d $$LOOP 2>/dev/null || true; fi; rmdir mnt 2>/dev/null || true; exit $$status; }; \
	trap cleanup EXIT; \
	LOOP=$$(sudo losetup -f --show -P $(DISK)); \
	mkdir -p mnt; \
	sudo mount $${LOOP}p1 mnt; \
	sudo cp $(CURDIR)/$(BUILD)/$(TARGET) mnt/boot/$(TARGET); \
	sudo mkdir -p mnt/FONTS; \
	sudo cp $(SRC)/assets/bmf/*.[bB][mM][fF] mnt/FONTS/ 2>/dev/null || true; \
	sudo mkdir -p mnt/IMAGES; \
	sudo cp images/*.[bB][mM][pP] mnt/IMAGES/ 2>/dev/null || true; \
	sudo mkdir -p mnt/ICONS; \
	sudo cp $(SRC)/assets/ico/*.[iI][cC][oO] mnt/ICONS/ 2>/dev/null || true; \
	sudo cp $(SRC)/assets/cur/*.[cC][uU][rR] mnt/ICONS/ 2>/dev/null || true; \
	sudo mkdir -p mnt/SOUNDS; \
	sudo cp $(SRC)/assets/wav/*.[wW][aA][vV] mnt/SOUNDS/ 2>/dev/null || true; \
	sudo mkdir -p mnt/OSLET; \
	sudo cp $(SRC)/assets/bmp/*.[bB][mM][pP] mnt/OSLET/ 2>/dev/null || true; \
	sudo cp $(SRC)/assets/pal/*.[pP][aA][lL] mnt/OSLET/ 2>/dev/null || true; \
	sudo umount mnt; \
	sudo losetup -d $$LOOP; \
	LOOP=""; \
	rmdir mnt; \
	trap - EXIT; \
	echo "Kernel and resources installed!"

binstall:
	@if [ ! -f "$(DISK)" ]; then \
		echo "Error: $(DISK) not found. Run 'make disk' first."; \
		exit 1; \
	fi
	@echo "Installing binaries..."
	@set -e; \
	LOOP=""; \
	cleanup() { status=$$?; sudo umount mnt 2>/dev/null || true; if [ -n "$$LOOP" ]; then sudo losetup -d $$LOOP 2>/dev/null || true; fi; rmdir mnt 2>/dev/null || true; exit $$status; }; \
	trap cleanup EXIT; \
	LOOP=$$(sudo losetup -f --show -P $(DISK)); \
	mkdir -p mnt; \
	sudo mount $${LOOP}p1 mnt; \
	for f in $(SHELL_TARGETS); do \
		if [ -f $(BUILD_BIN)/$$f ]; then \
			sudo cp $(BUILD_BIN)/$$f mnt/; \
		fi; \
	done; \
	sudo mkdir -p mnt/OSLET/START; \
	for f in $(AGIX_TARGETS) $(GIX_TARGETS); do \
		if [ -f $(BUILD_BIN)/$$f ]; then \
			sudo cp $(BUILD_BIN)/$$f mnt/OSLET/START/; \
		fi; \
	done; \
	sudo cp $(SRC)/ini/*.[iI][nN][iI] mnt/OSLET/ 2>/dev/null || true; \
	sudo cp $(SRC)/grp/*.[gG][rR][pP] mnt/OSLET/START/ 2>/dev/null || true; \
	sudo umount mnt; \
	sudo losetup -d $$LOOP; \
	LOOP=""; \
	rmdir mnt; \
	trap - EXIT; \
	echo "Binaries installed!"

update:
	@$(MAKE) kernel
	@$(MAKE) binaries
	@$(MAKE) install
	@$(MAKE) binstall

run:
	@if [ ! -f "$(DISK)" ]; then \
		echo "Error: $(DISK) not found. Run 'make full' once to create it."; \
		exit 1; \
	fi
	qemu-system-i386 \
	-drive file=$(DISK),format=raw \
	-m 32M \
	-net none \
	-vga std \
	-display $(QEMU_DISPLAY) \
	$(QEMU_SOUND) \
	-rtc base=localtime

run-ahci:
	@if [ ! -f "$(DISK)" ]; then \
		echo "Error: $(DISK) not found. Run 'make full' once to create it."; \
		exit 1; \
	fi
	qemu-system-i386 \
	-m 32M \
	-net none \
	-vga std \
	-display $(QEMU_DISPLAY) \
	$(QEMU_SOUND) \
	-rtc base=localtime \
	-drive if=none,id=sata0,file=$(DISK),format=raw \
	-device ich9-ahci,id=ahci \
	-device ide-hd,drive=sata0,bus=ahci.0

full:
	@echo "WARNING: full rebuilds a fresh disk image before running."
	@$(MAKE) fresh-disk
	@$(MAKE) update
	@$(MAKE) run

run-usb:
	@if [ ! -f "$(DISK)" ]; then \
		echo "Error: $(DISK) not found. Run 'make full' once to create it."; \
		exit 1; \
	fi
	qemu-system-i386 \
	-m 32M \
	-net none \
	-vga std \
	-display $(QEMU_DISPLAY) \
	$(QEMU_SOUND) \
	-rtc base=localtime \
	-boot menu=on \
	-drive if=none,id=usbstick,file=$(DISK),format=raw \
	-device qemu-xhci,id=usb \
	-device usb-storage,bus=usb.0,drive=usbstick,bootindex=1,removable=on

clean:
	@echo "Cleaning..."
	rm -rf $(BUILD)

clean-all: clean
	rm -f $(DISK)

-include $(DEP_FILES)
