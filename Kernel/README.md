# Kernel

凡进 **`Kernel.elf`** 的源码都在本树。顶层 **`Boot/`** 只放独立 EFI。

## 上电到 KernelMain

```text
加电
  ├─ X64:  UEFI → BOOTX64.EFI → 读盘 Kernel.elf
  │         → Hal/X64/KernelEntry.S   （换早期栈）
  │         → Hal/X64/KernelHandoff.c （UEFI → BOOT_INFO）
  │
  ├─ Arm64/RiscV: 加载器把 Kernel.elf 放入内存
  │         → Hal/<Arch>/KernelEntry.S
  │         → Hal/<Arch>/KernelHandoff.c （设备树等 → BOOT_INFO）
  │
  └─ 合流 → Core/KernelMain.c
              → 早期串口 / 记下帧缓冲配置
              → ModulesRun（模块表；当前仅 Serial）
              → park（后续模块未挂上时停在这里）
```

带 **【初学者】** 注释的源码，建议从 `Core/KernelMain.c` 与各 Arch 的 `KernelEntry.S` 读起。

## 目录要点

| 路径 | 说明 |
| ---- | ---- |
| `Include/Hal/` | 公共 HAL 门面（Common 只认这些头） |
| `Hal/<Arch>/` | 该架构入口、串口、页表等实现 |
| `Hal/<Arch>/Hal/` | 该架构私有细节（如 `HalPort.h`），见该目录 README |
| `Hal/Common/` | 跨 Arch HAL（`HalCapability`、`HalVideoStub`、DTB/RamFB 头） |
| `Core/` | 合流：`KernelMain`、模块表、PMM、`BOOT_INFO`（无 `Hal*` 实现） |
| `Hal/X64/HalVideo.c` | X64 直写帧缓冲（DrawPixel / FillRect） |
| `Core/Font.c` | 8×8 ASCII 点阵 → `HalVideoDrawStringAt` |
| `Hal/X64/HalSerialGop.c` | `SCREEN_LOG=1` 时 boot 日志上滚到 FB |
| `Core/PhysicalMemory.c` | PMM：BOOT_INFO 自由区上的位图页分配 |
| `Core/Device.c` | 设备框架空壳（K5） |
| `Core/VirtualMemory.c` | 认领 EarlyIdentity 分页；确认 FB 在窗内 |

## 编译

```bash
cd ~/Blocks/Kernel
./build.sh x64|arm64|riscv   # → Build/<Arch>/Kernel.elf
./build.sh x64 SERIAL=0      # 编译期关掉 UART
./build.sh x64 SCREEN_LOG=1  # boot 日志画到屏幕（分通道 TOY_SCREEN_LOG_*）
```
