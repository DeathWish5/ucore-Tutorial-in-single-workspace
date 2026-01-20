# rCore Tutorial C 版本 - 顶层 Makefile
#
# 使用方式：
#   make build CH=2      构建 chapter 2
#   make run CH=2        运行 chapter 2
#   make clean CH=2      清理 chapter 2
#   make clean-all       清理所有 chapter
#
# 有效的 chapter: 1, 2, 3, 4, 5, 6, 7, 8

# 默认 chapter
CH ?= 1

# 有效的 chapter 列表
CHAPTERS = 1 2 3 4 5 6 7 8

# 检查 CH 参数是否有效
ifeq ($(filter $(CH),$(CHAPTERS)),)
$(error Invalid chapter: $(CH). Valid chapters are: $(CHAPTERS))
endif

# 目标目录
TARGET_DIR = ch$(CH)

.PHONY: build run clean clean-all help user

# 构建指定 chapter
build:
	@echo "=========================================="
	@echo "  Building Chapter $(CH)"
	@echo "=========================================="
	$(MAKE) -C $(TARGET_DIR) build

# 运行指定 chapter
run:
	@echo "=========================================="
	@echo "  Running Chapter $(CH)"
	@echo "=========================================="
	$(MAKE) -C $(TARGET_DIR) run

# 清理指定 chapter
clean:
	@echo "Cleaning Chapter $(CH)..."
	$(MAKE) -C $(TARGET_DIR) clean

# 构建用户程序
user:
	@echo "Building user programs..."
	$(MAKE) -C user clean
	$(MAKE) -C user

# 清理所有 chapter
clean-all:
	@echo "Cleaning all chapters..."
	@for ch in $(CHAPTERS); do \
		if [ -d "ch$$ch" ]; then \
			echo "  Cleaning ch$$ch..."; \
			$(MAKE) -C ch$$ch clean 2>/dev/null || true; \
		fi \
	done
	@echo "  Cleaning user..."
	$(MAKE) -C user clean 2>/dev/null || true
	@echo "Done."

# 帮助信息
help:
	@echo "rCore Tutorial C Version - Build System"
	@echo ""
	@echo "Usage:"
	@echo "  make build CH=<n>    Build chapter n (default: CH=1)"
	@echo "  make run CH=<n>      Run chapter n in QEMU"
	@echo "  make clean CH=<n>    Clean chapter n build files"
	@echo "  make clean-all       Clean all chapters"
	@echo "  make user            Build user programs"
	@echo "  make help            Show this help message"
	@echo ""
	@echo "Available chapters: $(CHAPTERS)"
	@echo ""
	@echo "Examples:"
	@echo "  make run CH=2        # Run chapter 2"
	@echo "  make build CH=8      # Build chapter 8"
	@echo "  make clean-all       # Clean everything"
