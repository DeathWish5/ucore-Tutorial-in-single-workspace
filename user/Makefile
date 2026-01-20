# 用户程序构建

CC = riscv64-linux-musl-gcc
OBJCOPY = riscv64-linux-musl-objcopy

BUILD_DIR = build

CFLAGS = -march=rv64gc -mabi=lp64d -mcmodel=medany \
         -ffreestanding -fno-common -nostdlib \
         -fno-builtin -fno-stack-protector \
         -fno-pic -fno-pie -Wall -g -I.

LDFLAGS = -T user_linker.ld -nostdlib -static -no-pie

# 所有用户程序
USER_APPS = 00hello_world 01store_fault 02power 03priv_inst 04priv_csr \
            05write_a 06write_b 07write_c 08power_3 09power_5 10power_7 11sleep \
            12forktest initproc user_shell filetest_simple cat_filea sig_simple

.PHONY: all clean $(USER_APPS)

all: $(USER_APPS)

$(USER_APPS): %: bin/%.c entry.S user.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(BUILD_DIR)/$@.elf entry.S user.c $<
	$(OBJCOPY) $(BUILD_DIR)/$@.elf --strip-all -O binary $(BUILD_DIR)/$@.bin

clean:
	rm -rf $(BUILD_DIR)
