# Easy File System 打包工具 Makefile

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11

TARGET = fs_pack

.PHONY: all clean

all: $(TARGET)

$(TARGET): fs_pack.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)
