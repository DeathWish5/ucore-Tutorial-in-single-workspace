# ch3 - 多道程序与时间片轮转

CC = riscv64-linux-musl-gcc
OBJCOPY = riscv64-linux-musl-objcopy
OBJDUMP = riscv64-linux-musl-objdump
QEMU = qemu-system-riscv64

BUILD_DIR = build
BIOS = ../../rustsbi-qemu.bin
USER_DIR = ../user

CFLAGS = -march=rv64gc -mabi=lp64d -mcmodel=medany \
         -ffreestanding -fno-common -nostdlib \
         -fno-builtin -fno-stack-protector \
         -fno-pic -fno-pie -Wall -g

LDFLAGS = -T linker.ld -nostdlib -static -no-pie

# 内核对象文件
KERNEL_OBJS = $(BUILD_DIR)/entry.o $(BUILD_DIR)/main.o

# 库对象文件
LIB_OBJS = $(BUILD_DIR)/sbi.o $(BUILD_DIR)/mem.o $(BUILD_DIR)/printf.o \
           $(BUILD_DIR)/linker.o $(BUILD_DIR)/linker_asm.o \
           $(BUILD_DIR)/context.o $(BUILD_DIR)/context_asm.o \
           $(BUILD_DIR)/syscall.o

ALL_OBJS = $(KERNEL_OBJS) $(LIB_OBJS) $(BUILD_DIR)/app.o

ELF = $(BUILD_DIR)/ch3.elf
BIN = $(BUILD_DIR)/ch3.bin

# ch3 用户程序 (step = 0x200000 = 2MB)
USER_APPS = 00hello_world 01store_fault 02power 03priv_inst 04priv_csr \
            05write_a 06write_b 07write_c 08power_3 09power_5 10power_7 11sleep

.PHONY: all build run clean user disasm

all: build

user:
	cd $(USER_DIR) && $(MAKE) clean && $(MAKE) $(USER_APPS)

$(BUILD_DIR)/app.S: user
	@mkdir -p $(BUILD_DIR)
	$(USER_DIR)/gen_app_asm.sh 0x80400000 0x200000 $@ \
		$(foreach app,$(USER_APPS),$(USER_DIR)/build/$(app).bin)

build: $(BIN)

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

$(BUILD_DIR)/linker_asm.o: ../linker/linker_asm.S
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

# app data
$(BUILD_DIR)/app.o: $(BUILD_DIR)/app.S
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(BIN)
	$(QEMU) -machine virt -nographic -bios $(BIOS) -kernel $< -smp 1 -m 64M

disasm: $(ELF)
	$(OBJDUMP) -d $< > $(BUILD_DIR)/kernel.disasm

clean:
	rm -rf $(BUILD_DIR)
	cd $(USER_DIR) && $(MAKE) clean
