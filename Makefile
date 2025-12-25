# TinyOS64 build system
# Requires: gcc (or x86_64-elf-gcc), binutils, grub-mkrescue, xorriso, qemu-system-x86_64

CROSS ?=
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
AR      := $(CROSS)ar

BUILD   := build
ISO_DIR := $(BUILD)/isofiles

KERNEL_ELF := $(BUILD)/kernel.elf
KERNEL_ISO := $(BUILD)/tinyos64.iso

USER_ELF   := $(BUILD)/init.elf
INITRAMFS_TAR := $(BUILD)/initramfs.tar
INITRAMFS_O   := $(BUILD)/initramfs.o

DISK_IMG := $(BUILD)/disk.img

CFLAGS := -std=c11 -O2 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -no-pie \
          -m64 -mno-red-zone -mgeneral-regs-only \
          -Wall -Wextra -Werror -Iinclude

LDFLAGS := -nostdlib -no-pie -Wl,-T,linker.ld -Wl,--build-id=none -Wl,-z,max-page-size=0x1000 -Wl,-z,noexecstack

USER_CFLAGS := -std=c11 -O2 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -no-pie \
               -m64 -mcmodel=large -Wall -Wextra -Werror -Iuser -Iinclude
USER_LDFLAGS := -nostdlib -no-pie -Wl,-T,user/user.ld -Wl,--build-id=none -Wl,-z,max-page-size=0x1000 -Wl,-z,noexecstack -Wl,-z,noexecstack

KERNEL_SRCS := \
    src/boot.S \
    src/kernel.c \
    src/console.c \
    src/serial.c \
    src/log.c \
    src/gdb.c \
    src/lib.c \
    src/pmm.c \
    src/vmm.c \
    src/kmalloc.c \
    src/vfs.c \
    src/memfs.c \
    src/devfs.c \
    src/input.c \
    src/tarfs.c \
    src/elf.c \
    src/pci.c \
    src/net.c \
    src/virtio_blk.c \
    src/disk.c \
    src/syscall.c \
    src/scheduler.c \
    src/rtc.c \
    src/time.c \
    src/hpet.c \
    src/arch/x86_64/gdt.c \
    src/arch/x86_64/idt.c \
    src/arch/x86_64/apic.c \
    src/arch/x86_64/cpu.c \
    src/arch/x86_64/mp.c \
    src/arch/x86_64/smp.c \
    src/arch/x86_64/pic.c \
    src/arch/x86_64/pit.c \
    src/arch/x86_64/irq.c \
    src/arch/x86_64/ap_trampoline.S \
    src/arch/x86_64/interrupts.S \
    src/arch/x86_64/interrupt_dispatch.c

KERNEL_OBJS := $(patsubst src/%, $(BUILD)/%, $(KERNEL_SRCS:.c=.o))
KERNEL_OBJS := $(KERNEL_OBJS:.S=.o)
KERNEL_OBJS += $(INITRAMFS_O)

.PHONY: all clean iso run

all: $(KERNEL_ELF)

$(BUILD):
	mkdir -p $(BUILD)

# Kernel objects
$(BUILD)/%.o: src/%.c | $(BUILD)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/%.S | $(BUILD)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Userland init.elf
$(USER_ELF): user/start.S user/init.c user/malloc.c user/lib.c user/user.ld | $(BUILD)
	$(CC) $(USER_CFLAGS) user/start.S user/init.c user/malloc.c user/lib.c -o $@ $(USER_LDFLAGS)

# Initramfs (tar) and embed object
$(INITRAMFS_TAR): $(USER_ELF) | $(BUILD)
	rm -rf $(BUILD)/initramfs_dir
	mkdir -p $(BUILD)/initramfs_dir
	cp $(USER_ELF) $(BUILD)/initramfs_dir/init.elf
	tar -C $(BUILD)/initramfs_dir -cf $@ .

$(INITRAMFS_O): $(INITRAMFS_TAR) | $(BUILD)
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

# Link kernel
$(KERNEL_ELF): $(KERNEL_OBJS) linker.ld | $(BUILD)
	$(CC) $(CFLAGS) $(KERNEL_OBJS) -o $@ $(LDFLAGS)

# ISO image (GRUB)
iso: $(KERNEL_ISO)

$(KERNEL_ISO): $(KERNEL_ELF) | $(BUILD)
	rm -rf $(ISO_DIR)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/kernel.elf
	printf 'set timeout=0\nset default=0\n\nmenuentry "TinyOS64" {\n  multiboot2 /boot/kernel.elf\n  boot\n}\n' > $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(ISO_DIR) > /dev/null

# Create a small raw disk image for virtio-blk tests
$(DISK_IMG): | $(BUILD)
	dd if=/dev/zero of=$@ bs=1M count=16 status=none
	printf 'TINYOS64_DISK\\n' | dd of=$@ conv=notrunc status=none

# QEMU run helper (forces legacy virtio-blk so our driver works out of the box)
run: iso $(DISK_IMG)
	qemu-system-x86_64 \
	  -m 512M \
	  -serial stdio \
	  -no-reboot \
	  -cdrom $(KERNEL_ISO) \
	  -drive file=$(DISK_IMG),format=raw,if=none,id=vdisk \
	  -device virtio-blk-pci,drive=vdisk,disable-modern=on

clean:
	rm -rf $(BUILD)
