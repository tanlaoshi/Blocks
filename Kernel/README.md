# Kernel

现网能力核（自 `ToyOS/ToyKernel` 迁入）。Boot 在旁挂 `../Boot`。

## 顶层（按依赖）

```text
HAL/        # 原 CodeA-HAL
Library/    # 原 CodeB-Library
Core/       # 原 CodeC（Core + Modules 策略 + 开机编织）
Services/   # 原 CodeD-Services
User/       # 原 CodeE-User
```

`Core/`：`Kernel.c` / `Modules.c` 编织；真肉夹（Scheduler/Process/Syscall/…）。开机 `Initialize*`：`HAL/Init/`（Serial/Cpu/Video/Usb/Smp）与 Services（Console/Gui/Net/FS）；策略在 `Scheduler/`、`FileSystem/Fat/` 等。

`Library/` 纯库：禁止依赖 Hal/Core/Services（`make check-library-pure`）。UI→`Services/Ui/`，Bmp→`Services/Bmp/`，字库加载/TTF→`Services/Fonts/`，Elf→`Core/Elf/`，驱动框架→`Core/Driver/`；`Library/Fonts/` 仅留点阵数据。

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
