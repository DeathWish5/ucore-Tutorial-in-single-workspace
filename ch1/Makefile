# ch1 - 最小化内核构建

CC = riscv64-linux-musl-gcc
OBJCOPY = riscv64-linux-musl-objcopy
QEMU = qemu-system-riscv64

BUILD_DIR = build
BIOS = ../rustsbi-qemu.bin

CFLAGS = -march=rv64gc -mabi=lp64d -mcmodel=medany \
         -ffreestanding -fno-common -nostdlib \
         -fno-builtin -fno-stack-protector \
         -fno-pic -fno-pie -Wall -g

LDFLAGS = -T linker.ld -nostdlib -static -no-pie

OBJS = $(BUILD_DIR)/entry.o $(BUILD_DIR)/main.o $(BUILD_DIR)/sbi.o

ELF = $(BUILD_DIR)/ch1.elf
BIN = $(BUILD_DIR)/ch1.bin

.PHONY: all build run clean

all: build

build: $(BIN)

$(BIN): $(ELF)
	$(OBJCOPY) $< --strip-all -O binary $@

$(ELF): $(OBJS) linker.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD_DIR)/main.o: main.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/entry.o: entry.S
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sbi.o: ../util/sbi.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(BIN)
	$(QEMU) -machine virt -nographic -bios $(BIOS) -kernel $< -smp 1 -m 64M

clean:
	rm -rf $(BUILD_DIR)
