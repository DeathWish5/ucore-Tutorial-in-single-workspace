# ch8 - 线程与同步

CC = riscv64-linux-musl-gcc
OBJCOPY = riscv64-linux-musl-objcopy
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

KERNEL_OBJS = $(BUILD_DIR)/entry.o $(BUILD_DIR)/main.o

LIB_OBJS = $(BUILD_DIR)/sbi.o $(BUILD_DIR)/mem.o $(BUILD_DIR)/printf.o \
           $(BUILD_DIR)/linker.o $(BUILD_DIR)/linker_stub.o \
           $(BUILD_DIR)/context.o $(BUILD_DIR)/context_asm.o \
           $(BUILD_DIR)/syscall.o $(BUILD_DIR)/heap.o \
           $(BUILD_DIR)/address_space.o $(BUILD_DIR)/elf.o \
           $(BUILD_DIR)/easy_fs.o $(BUILD_DIR)/virtio_block.o \
           $(BUILD_DIR)/signal.o $(BUILD_DIR)/sync.o

ALL_OBJS = $(KERNEL_OBJS) $(LIB_OBJS)

ELF = $(BUILD_DIR)/ch8.elf
BIN = $(BUILD_DIR)/ch8.bin

.PHONY: all build run clean user

all: build

user:
	cd $(USER_DIR) && $(MAKE) clean && $(MAKE)

$(FS_IMG): user
	@mkdir -p $(BUILD_DIR)
	cd ../.. && cargo run --release -p xtask -- make --ch 8
	cp ../../target/riscv64gc-unknown-none-elf/debug/fs.img $(FS_IMG)

build: $(BIN) $(FS_IMG)

$(BIN): $(ELF)
	$(OBJCOPY) $< --strip-all -O binary $@

$(ELF): $(ALL_OBJS) linker.ld
	$(CC) $(LDFLAGS) -o $@ $(ALL_OBJS)

$(BUILD_DIR)/entry.o: entry.S
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/main.o: main.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sbi.o: ../util/sbi.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/mem.o: ../util/mem.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/printf.o: ../util/printf.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/linker.o: ../linker/linker.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/linker_stub.o: linker_stub.S
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/context.o: ../kernel-context/context.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/context_asm.o: ../kernel-context/context_asm.S
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/syscall.o: ../syscall/syscall.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/heap.o: ../kernel-alloc/heap.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/address_space.o: ../kernel-vm/address_space.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/elf.o: ../kernel-vm/elf.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/easy_fs.o: ../easy-fs/easy_fs.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/virtio_block.o: ../virtio-block/virtio_block.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/signal.o: ../signal/signal.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sync.o: ../sync/sync.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(BIN) $(FS_IMG)
	$(QEMU) -machine virt -nographic -bios $(BIOS) -kernel $< -smp 1 -m 64M \
		-drive file=$(FS_IMG),if=none,format=raw,id=x0 \
		-device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

clean:
	rm -rf $(BUILD_DIR)
