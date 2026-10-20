# Kernel

现网能力核（自 `ToyOS/ToyKernel` 迁入）。Boot 在旁挂 `../Boot`。

## 顶层

结构示意（同心圆）：[Documents/项目/Kernel-rings.png](../Documents/项目/Kernel-rings.png)  
阅读顺序（先骨后肉）：[Documents/手册/仓库与启动/骨架与肉.md](../Documents/手册/仓库与启动/骨架与肉.md)

```text
BringUp/    # 阶段2：<Arch>/Entry·Handoff + KernelMain/Modules（读启动段只看这里）
HAL/        # Hal* 门面与驱动；Init/ 为模块表里的硬件胶水
Library/    # 共用纯库（无 Hal/Core/Services）
Core/       # 内核机制（调度/进程/PMM/VMM/syscall…）
Services/   # 人机与会话（与 Core 平级）
User/       # Ring3 应用与用户态 SDK
```

控制流：`Boot`/固件 → **`BringUp/<Arch>/KernelEntry` → Handoff → KernelMain`** → `ModulesRun` → …

`Library/` 纯化：`make check-library-pure`。几何库在 `Library/Graphics`。UI→`Services/Ui/`，Bmp→`Services/Bmp/`，字库加载→`Services/Fonts/`，Elf→`Core/Elf/`。

## 编译

```bash
cd ~/Blocks/Kernel
./build.sh              # x64 → Build/HAL/X64/Kernel.elf
./build.sh arm64        # → Build/HAL/Arm64/Kernel.elf
./build.sh riscv        # → Build/HAL/RiscV/Kernel.elf
```

交叉链：`../Tools/xpack-*`。

## 手测（X64 桌面）

```bash
cd ~/Blocks/Runtime && ./run.sh --headless
```
