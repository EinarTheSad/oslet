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

KERNEL_SUBDIRS = arch drivers fonts irq mem task win

KERNEL_ROOT_OBJS = $(CURDIR)/$(BUILD)/kernel.o \
                  $(CURDIR)/$(BUILD)/console.o \
                  $(CURDIR)/$(BUILD)/vconsole.o \
                  $(CURDIR)/$(BUILD)/rtc.o \
                  $(CURDIR)/$(BUILD)/shell_kernel.o \
                  $(CURDIR)/$(BUILD)/syscall.o

KERNEL_SUBDIR_SRCS_C := $(foreach dir,$(KERNEL_SUBDIRS),$(wildcard $(SRC)/$(dir)/*.c))
KERNEL_SUBDIR_SRCS_S := $(foreach dir,$(KERNEL_SUBDIRS),$(wildcard $(SRC)/$(dir)/*.S))
KERNEL_SUBDIR_OBJS := $(patsubst $(SRC)/%.c,$(CURDIR)/$(BUILD)/%.o,$(KERNEL_SUBDIR_SRCS_C)) \
                      $(patsubst $(SRC)/%.S,$(CURDIR)/$(BUILD)/%.o,$(KERNEL_SUBDIR_SRCS_S))

KERNEL_OBJS = $(KERNEL_ROOT_OBJS) $(KERNEL_SUBDIR_OBJS)

LIB_OBJ_DIR      = $(CURDIR)/$(BUILD)/lib
LIB_SRCS         = $(filter-out $(SRC)/lib/elf.c $(SRC)/lib/mode_marker.c,$(wildcard $(SRC)/lib/*.c))
LIB_OBJS         = $(patsubst $(SRC)/lib/%.c,$(LIB_OBJ_DIR)/%.o,$(LIB_SRCS))

BUILD_BIN        = $(CURDIR)/$(BUILD)/bin

SHELL_OBJ_DIR    = $(CURDIR)/$(BUILD)/obj/shell
APPS_AGIX_OBJ_DIR = $(CURDIR)/$(BUILD)/obj/apps/agix
APPS_GIX_OBJ_DIR = $(CURDIR)/$(BUILD)/obj/apps/gix

SHELL_SRCS       := $(filter-out $(SRC)/shell/cpl_%.c $(SRC)/shell/progman.c $(SRC)/shell/startman.c $(SRC)/shell/textmode.c $(SRC)/shell/shutdown.c,$(wildcard $(SRC)/shell/*.c))
SHELL_TARGETS     := $(patsubst $(SRC)/shell/%.c,%.elf,$(SHELL_SRCS))

AGIX_SRCS        := $(wildcard $(SRC)/apps/agix/*.c)
AGIX_TARGETS     := $(patsubst $(SRC)/apps/agix/%.c,%.elf,$(AGIX_SRCS))

GIX_SRCS         := $(wildcard $(SRC)/apps/gix/*.c)
GIX_TARGETS      := $(patsubst $(SRC)/apps/gix/%.c,%.elf,$(GIX_SRCS))

PROGRAM_NAMES    := $(foreach t,$(SHELL_TARGETS) $(AGIX_TARGETS) $(GIX_TARGETS),$(basename $(t)))

.PHONY: all run clean clean-all disk install binstall binaries full kernel-build programs $(PROGRAM_NAMES)

all: kernel-build $(CURDIR)/$(BUILD)/$(TARGET)

kernel: $(CURDIR)/$(BUILD)/$(TARGET)

$(CURDIR)/$(BUILD)/$(TARGET): $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

kernel-build:
	@$(MAKE) -C $(SRC) BUILD=$(CURDIR)/$(BUILD) CFLAGS="$(CFLAGS)"
	@$(MAKE) -C $(SRC)/arch BUILD=$(CURDIR)/$(BUILD) CFLAGS="$(CFLAGS)"
	@$(MAKE) -C $(SRC)/drivers BUILD=$(CURDIR)/$(BUILD) CFLAGS="$(CFLAGS)"
	@$(MAKE) -C $(SRC)/fonts BUILD=$(CURDIR)/$(BUILD) CFLAGS="$(CFLAGS)"
	@$(MAKE) -C $(SRC)/irq BUILD=$(CURDIR)/$(BUILD) CFLAGS="$(CFLAGS)"
	@$(MAKE) -C $(SRC)/mem BUILD=$(CURDIR)/$(BUILD) CFLAGS="$(CFLAGS)"
	@$(MAKE) -C $(SRC)/task BUILD=$(CURDIR)/$(BUILD) CFLAGS="$(CFLAGS)"
	@$(MAKE) -C $(SRC)/win BUILD=$(CURDIR)/$(BUILD) CFLAGS="$(CFLAGS)"

$(CURDIR)/$(BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(CURDIR)/$(BUILD)/%.o: $(SRC)/%.S
	@mkdir -p $(dir $@)
	$(CC) -m32 -c $< -o $@

$(CURDIR)/$(BUILD)/obj/shell/%.o: $(SRC)/shell/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -c $< -o $@

shell-build:
	@$(MAKE) -C $(SRC)/shell BUILD=$(CURDIR)/$(BUILD) BINCFLAGS="$(BINCFLAGS)"

agix-build:
	@$(MAKE) -C $(SRC)/apps/agix BUILD=$(CURDIR)/$(BUILD) BINCFLAGS="$(BINCFLAGS)"

gix-build:
	@$(MAKE) -C $(SRC)/apps/gix BUILD=$(CURDIR)/$(BUILD) BINCFLAGS="$(BINCFLAGS)"

lib-build:
	@$(MAKE) -C $(SRC)/lib BUILD=$(CURDIR)/$(BUILD) BINCFLAGS="$(BINCFLAGS)"

programs: lib-build shell-build agix-build gix-build

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
	$(LD) -m elf_i386 -T $(SRC)/shell/binary.ld -nostdlib -pie -o $@ $^

$(BUILD_BIN)/shell.elf: $(SHELL_OBJ_DIR)/shell.o $(LIB_OBJS) $(LIB_OBJ_DIR)/elf.o $(LIB_OBJ_DIR)/mode_marker_agix.o
	@mkdir -p $(BUILD_BIN)
	$(LD) -m elf_i386 -T $(SRC)/shell/binary.ld -nostdlib -pie -o $@ $^

GIX_TARGETS_NOEXT := $(patsubst $(SRC)/apps/gix/%.c,%,$(GIX_SRCS))
AGIX_TARGETS_NOEXT := $(patsubst $(SRC)/apps/agix/%.c,%,$(AGIX_SRCS))

$(GIX_TARGETS_NOEXT:%=$(BUILD_BIN)/%.elf): $(BUILD_BIN)/%.elf: $(LIB_OBJS) $(LIB_OBJ_DIR)/elf.o $(LIB_OBJ_DIR)/mode_marker_gix.o
	@mkdir -p $(APPS_GIX_OBJ_DIR)
	$(CC) $(BINCFLAGS) -DGIX_BUILD -c $(SRC)/apps/gix/$*.c -o $(APPS_GIX_OBJ_DIR)/$*.o
	@mkdir -p $(BUILD_BIN)
	$(LD) -m elf_i386 -T $(SRC)/shell/binary.ld -nostdlib -pie -o $@ $(APPS_GIX_OBJ_DIR)/$*.o $(LIB_OBJS) $(LIB_OBJ_DIR)/elf.o $(LIB_OBJ_DIR)/mode_marker_gix.o

$(AGIX_TARGETS_NOEXT:%=$(BUILD_BIN)/%.elf): $(BUILD_BIN)/%.elf: $(LIB_OBJS) $(LIB_OBJ_DIR)/mode_marker_agix.o
	@mkdir -p $(APPS_AGIX_OBJ_DIR)
	$(CC) $(BINCFLAGS) -DAGIX_BUILD -c $(SRC)/apps/agix/$*.c -o $(APPS_AGIX_OBJ_DIR)/$*.o
	@mkdir -p $(BUILD_BIN)
	$(LD) -m elf_i386 -T $(SRC)/shell/binary.ld -nostdlib -pie -o $@ $(APPS_AGIX_OBJ_DIR)/$*.o $(LIB_OBJS) $(LIB_OBJ_DIR)/mode_marker_agix.o

binaries: programs
	@for t in $(filter-out desktop.elf shell.elf,$(SHELL_TARGETS)); do \
		$(MAKE) $(BUILD_BIN)/$$t; \
	done
	$(MAKE) $(BUILD_BIN)/desktop.elf $(BUILD_BIN)/shell.elf
	@for t in $(AGIX_TARGETS); do \
		$(MAKE) $(BUILD_BIN)/$$t; \
	done
	@for t in $(GIX_TARGETS); do \
		$(MAKE) $(BUILD_BIN)/$$t; \
	done

$(PROGRAM_NAMES): lib-build
	@$(MAKE) $(BUILD_BIN)/$@.elf

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

install: $(CURDIR)/$(BUILD)/$(TARGET)
	@if [ ! -f "$(DISK)" ]; then \
		echo "Error: $(DISK) not found. Run 'make disk' first."; \
		exit 1; \
	fi
	@echo "Installing kernel and resources..."
	@LOOP=$$(sudo losetup -f --show -P $(DISK)); \
	mkdir -p mnt; \
	sudo mount $${LOOP}p1 mnt; \
	sudo cp $(CURDIR)/$(BUILD)/$(TARGET) mnt/boot/$(TARGET); \
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
	for f in $(AGIX_TARGETS) $(GIX_TARGETS); do \
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

full: disk binaries install binstall run

run: $(CURDIR)/$(BUILD)/$(TARGET)
	@if [ ! -f "$(DISK)" ]; then \
		$(MAKE) disk; \
	fi
	@$(MAKE) install
	qemu-system-i386 \
	-drive file=$(DISK),format=raw \
	-m 32M \
	-net none \
	-vga std \
	-rtc base=localtime \
	#-audiodev sdl,id=snd0 \
    #-device sb16,audiodev=snd0

clean:
	@echo "Cleaning..."
	rm -rf $(BUILD)

clean-all: clean
	rm -f $(DISK)
