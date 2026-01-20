# rCore Tutorial - C 语言版本

这是 [rCore-Tutorial](https://github.com/rcore-os/rCore-Tutorial-v3) 的 C 语言移植版本，旨在让不熟悉 Rust 的学习者也能学习操作系统内核开发。

## 项目结构

```
c/
├── ch1/                 # Chapter 1: 裸机环境与 SBI
├── ch2/                 # Chapter 2: 批处理系统
├── ch3/                 # Chapter 3: 多道程序与分时多任务
├── ch4/                 # Chapter 4: 地址空间（虚拟内存）
├── ch5/                 # Chapter 5: 进程
├── ch6/                 # Chapter 6: 文件系统
├── ch7/                 # Chapter 7: 进程间通信（信号）
├── ch8/                 # Chapter 8: 线程与同步
├── user/                # 用户态程序
├── kernel-alloc/        # 内核堆分配器
├── kernel-context/      # 上下文切换
├── kernel-vm/           # 虚拟内存管理
├── linker/              # 链接脚本相关
├── syscall/             # 系统调用框架
├── easy-fs/             # 简易文件系统
├── virtio-block/        # VirtIO 块设备驱动
├── signal/              # 信号处理
├── sync/                # 同步原语
└── util/                # 工具函数（printf, sbi 等）
```

## 环境要求

- **交叉编译器**: `riscv64-linux-musl-gcc` (推荐) 或 `riscv64-unknown-elf-gcc`
- **QEMU**: `qemu-system-riscv64` (版本 >= 5.0)
- **RustSBI**: 项目根目录下的 `rustsbi-qemu.bin`
- **Rust 工具链**: 用于生成 `fs.img` (ch6-ch8)

### 安装交叉编译器 (Ubuntu/Debian)

```bash
# 方法 1: 使用 musl 工具链 (推荐)
wget https://musl.cc/riscv64-linux-musl-cross.tgz
tar xf riscv64-linux-musl-cross.tgz
export PATH=$PATH:$(pwd)/riscv64-linux-musl-cross/bin

# 方法 2: 使用发行版包
sudo apt install gcc-riscv64-unknown-elf
```

### 安装 QEMU

```bash
sudo apt install qemu-system-misc
```

## 快速开始

```bash
# 进入 C 目录
cd c

# 构建并运行 chapter 1
make run CH=1

# 构建并运行 chapter 2
make run CH=2

# 构建 chapter 8
make build CH=8

# 清理所有构建文件
make clean-all
```

## 各章节功能

| Chapter | 功能 | 关键特性 |
|---------|------|---------|
| ch1 | 裸机环境 | SBI 调用、串口输出 |
| ch2 | 批处理系统 | 特权级切换、系统调用 |
| ch3 | 多道程序 | 任务切换、协作式调度 |
| ch4 | 地址空间 | Sv39 分页、虚拟内存 |
| ch5 | 进程 | fork/exec/waitpid |
| ch6 | 文件系统 | VirtIO 块设备、easy-fs |
| ch7 | 信号 | 信号处理、sigaction |
| ch8 | 线程与同步 | 线程、互斥锁、信号量、条件变量 |

## 与 Rust 版本的差异

1. **泛型处理**: C 版本使用宏和函数指针替代 Rust 的泛型
2. **内存安全**: 需要手动管理内存，无编译期借用检查
3. **错误处理**: 使用返回值而非 Result/Option 类型
4. **模块系统**: 使用头文件而非 Rust 的 mod 系统

## 调试

每个 chapter 目录下都可以使用 GDB 调试：

```bash
# 终端 1: 启动 QEMU 并等待 GDB 连接
cd ch2
qemu-system-riscv64 -machine virt -nographic -bios ../../rustsbi-qemu.bin \
    -kernel build/ch2.bin -s -S

# 终端 2: 启动 GDB
riscv64-unknown-elf-gdb build/ch2.elf
(gdb) target remote :1234
(gdb) break main
(gdb) continue
```

## 用户程序

用户程序位于 `user/bin/` 目录：

```bash
# 单独构建用户程序
make user

# 用户程序列表
user/bin/
├── 00hello_world.c      # Hello World
├── 01store_fault.c      # 存储错误测试
├── 02power.c            # 幂运算
├── 12forktest.c         # fork 测试
├── initproc.c           # 初始进程
├── user_shell.c         # 用户 Shell
├── filetest_simple.c    # 文件系统测试
├── sig_simple.c         # 信号测试
└── ...
```

## 已知限制

1. **帧分配器**: 当前使用简单的堆分配，未实现真正的物理帧分配器
2. **内存释放**: `as_destroy` 和 `heap_free` 为空实现（内存泄漏）
3. **fs.img**: ch6-ch8 使用 Rust xtask 生成的 fs.img

## 参考资料

- [rCore-Tutorial-Book-v3](https://rcore-os.cn/rCore-Tutorial-Book-v3/)
- [RISC-V 特权级规范](https://riscv.org/specifications/privileged-isa/)
- [xv6-riscv](https://github.com/mit-pdos/xv6-riscv)

## License

MIT License
