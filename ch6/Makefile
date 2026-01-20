# ch6 - 文件系统

CC = riscv64-linux-musl-gcc
OBJCOPY = riscv64-linux-musl-objcopy
OBJDUMP = riscv64-linux-musl-objdump
QEMU = qemu-system-riscv64

BUILD_DIR = build
BIOS = ../../rustsbi-qemu.bin
USER_DIR = ../user
FS_IMG = $(BUILD_DIR)/fs.img

CFLAGS = -march=rv64gc -mabi=lp64d -mcmodel=medany \
         -ffreestanding -fno-common -nostdlib \
         -fno-builtin -fno-stack-protector \
         -fno-pic -fno-pie -Wall -g

LDFLAGS = -T linker.ld -nostdlib -static -no-pie

# 内核对象文件
KERNEL_OBJS = $(BUILD_DIR)/entry.o $(BUILD_DIR)/main.o

# 库对象文件 (ch6 不需要 linker_asm.o 因为从文件系统加载)
LIB_OBJS = $(BUILD_DIR)/sbi.o $(BUILD_DIR)/mem.o $(BUILD_DIR)/printf.o \
           $(BUILD_DIR)/linker.o $(BUILD_DIR)/linker_stub.o \
           $(BUILD_DIR)/context.o $(BUILD_DIR)/context_asm.o \
           $(BUILD_DIR)/syscall.o \
           $(BUILD_DIR)/heap.o \
           $(BUILD_DIR)/address_space.o $(BUILD_DIR)/elf.o \
           $(BUILD_DIR)/proc_manage.o \
           $(BUILD_DIR)/easy_fs.o \
           $(BUILD_DIR)/virtio_block.o

ALL_OBJS = $(KERNEL_OBJS) $(LIB_OBJS)

ELF = $(BUILD_DIR)/ch6.elf
BIN = $(BUILD_DIR)/ch6.bin

# ch6 应用程序列表（从文件系统加载）
USER_APPS = 00hello_world 02power 12forktest initproc user_shell filetest_simple cat_filea

.PHONY: all build run clean user fs disasm

all: build

user:
	cd $(USER_DIR) && $(MAKE) clean && $(MAKE) $(USER_APPS)

# 创建文件系统镜像（使用 Rust 工具，但文件系统格式兼容）
$(FS_IMG): user
	@mkdir -p $(BUILD_DIR)
	@echo "Creating fs.img using Rust xtask tool..."
	cd ../.. && cargo run --release -p xtask -- make --ch 6
	@echo "Copying fs.img..."
	cp ../../target/riscv64gc-unknown-none-elf/debug/fs.img $(FS_IMG)

build: $(BIN) $(FS_IMG)

$(BIN): $(ELF)
	$(OBJCOPY) $< --strip-all -O binary $@

$(ELF): $(ALL_OBJS) linker.ld
	$(CC) $(LDFLAGS) -o $@ $(ALL_OBJS)

# 内核
$(BUILD_DIR)/entry.o: entry.S
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/main.o: main.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# util
$(BUILD_DIR)/sbi.o: ../util/sbi.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/mem.o: ../util/mem.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/printf.o: ../util/printf.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# linker
$(BUILD_DIR)/linker.o: ../linker/linker.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# ch6 使用 stub 代替 linker_asm (不需要嵌入式应用)
$(BUILD_DIR)/linker_stub.o: linker_stub.S
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# context
$(BUILD_DIR)/context.o: ../kernel-context/context.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/context_asm.o: ../kernel-context/context_asm.S
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# syscall
$(BUILD_DIR)/syscall.o: ../syscall/syscall.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# kernel-alloc
$(BUILD_DIR)/heap.o: ../kernel-alloc/heap.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# kernel-vm
$(BUILD_DIR)/address_space.o: ../kernel-vm/address_space.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/elf.o: ../kernel-vm/elf.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# task-manage
$(BUILD_DIR)/proc_manage.o: ../task-manage/proc_manage.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# easy-fs
$(BUILD_DIR)/easy_fs.o: ../easy-fs/easy_fs.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# virtio-block
$(BUILD_DIR)/virtio_block.o: ../virtio-block/virtio_block.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(BIN) $(FS_IMG)
	$(QEMU) -machine virt -nographic -bios $(BIOS) -kernel $< -smp 1 -m 64M \
		-drive file=$(FS_IMG),if=none,format=raw,id=x0 \
		-device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

disasm: $(ELF)
	$(OBJDUMP) -d $< > $(BUILD_DIR)/kernel.disasm

clean:
	rm -rf $(BUILD_DIR)
	cd $(USER_DIR) && $(MAKE) clean
