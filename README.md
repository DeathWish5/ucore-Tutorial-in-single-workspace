# rCore Tutorial - C 语言版本

这是 [rCore-Tutorial](https://github.com/rcore-os/rCore-Tutorial-v3) 的 C 语言移植版本。

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
├── fs-pack/             # 文件系统镜像打包工具
├── util/                # 工具函数（printf, sbi 等）
└── rustsbi-qemu.bin     # RustSBI QEMU 固件
```

## 环境要求

- **交叉编译器**: `riscv64-linux-musl-gcc` (推荐) 或 `riscv64-unknown-elf-gcc`
- **QEMU**: `qemu-system-riscv64` (版本 >= 5.0)
- **主机编译器**: `gcc` (用于构建 `fs_pack` 工具)

### 安装交叉编译器 (Ubuntu/Debian)

```bash
# musl 工具链
wget https://more.musl.cc/9.2.1-20190831/x86_64-linux-musl/riscv64-linux-musl-cross.tgz
# 2021.3.1更新：上面的网站发生了"Catastrophic disk failure"，暂时处于不可用状态。可从清华云盘下载：https://cloud.tsinghua.edu.cn/f/cc4af959a6fc469e8564/
tar xzvf riscv64-linux-musl-cross.tgz
export PATH=$PATH:$(path to cross)/riscv64-linux-musl-cross/bin

# gdb 等其他工具链（非必须）
wget https://static.dev.sifive.com/dev-tools/freedom-tools/v2020.08/riscv64-unknown-elf-gcc-10.1.0-2020.08.2-x86_64-linux-ubuntu14.tar.gz
tar xzvf riscv64-unknown-elf-gcc-10.1.0-2020.08.2-x86_64-linux-ubuntu14.tar.gz
mv riscv64-unknown-elf-gcc-10.1.0-2020.08.2-x86_64-linux-ubuntu14 riscv64-unknown-elf-gcc
export PATH=$PATH:$(path to cross)/riscv64-unknown-elf-gcc/bin
```

### 安装 QEMU

可参考：https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter0/5setup-devel-env.html#id2

### RustSBI QEMU

项目根目录下的 `rustsbi-qemu.bin` 是 RISC-V SBI 固件，用于在 QEMU 中引导内核。
该文件已包含在仓库中，无需额外下载。

如需手动获取，可从 [RustSBI 发布页](https://github.com/rustsbi/rustsbi-qemu/releases) 下载。

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

// TODO ...

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

## 参考资料

- [rCore-Tutorial-Book-v3](https://rcore-os.cn/rCore-Tutorial-Book-v3/)
- [uCore-Tutorial-Book-v3](https://github.com/DeathWish5/ucore-Tutorial-Book/blob/main/lab0/%E5%AE%9E%E9%AA%8C%E7%8E%AF%E5%A2%83%E9%85%8D%E7%BD%AE.md)
- [RISC-V 特权级规范](https://riscv.org/specifications/privileged-isa/)

## License

MIT License
