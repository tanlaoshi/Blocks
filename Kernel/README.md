# Kernel

现网能力核（自 `ToyOS/ToyKernel` 迁入）。Boot 在旁挂 `../Boot`。

## 顶层

结构示意（同心圆）：[Documents/项目/Kernel-rings.png](../Documents/项目/Kernel-rings.png)

```text
BringUp/    # 阶段2导演：KernelMain / Modules（HAL 交棒后合流）
HAL/        # Entry·Handoff·驱动；Init/ 为开机胶水
Library/    # 共用纯库（无 Hal/Core/Services）
Core/       # 内核机制（调度/进程/PMM/VMM/syscall…）
Services/   # 人机与会话（与 Core 平级）
User/       # Ring3 应用与用户态 SDK
```

控制流：`Boot` 或固件 → `HAL/<Arch>/KernelEntry` → `KernelHandoff` → **`BringUp/KernelMain`** → `ModulesRun` → …

`Library/` 纯化：`make check-library-pure`。UI→`Services/Ui/`，Bmp→`Services/Bmp/`，字库加载→`Services/Fonts/`，Elf→`Core/Elf/`。

## 编译

```bash
cd ~/Blocks/Kernel
./build.sh              # x64 → Build/HAL/X64/Kernel.elf
./build.sh arm64        # → Build/HAL/Arm64/Kernel.elf
./build.sh riscv        # → Build/HAL/RiscV/Kernel.elf
```

交叉链：`../Tools/Extract/xpack-*`。

## 手测（X64 桌面）

```bash
cd ~/Blocks/Runtime && ./run.sh --headless
```
