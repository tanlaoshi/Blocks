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
  └─ 合流 → Core/Kernel.c（入口函数仍叫 KernelMain）
              → 早期串口 / 记下帧缓冲配置
              → ModulesRunFull（模块表）
              → park
```

带 **【初学者】** 注释的源码，建议从 `Core/Kernel.c` 与各 Arch 的 `KernelEntry.S` 读起。

## 目录要点

| 路径 | 说明 |
| ---- | ---- |
| `Include/Hal/` | 公共 HAL 门面（Common 只认这些头） |
| `Hal/<Arch>/` | 该架构入口、串口、页表等实现 |
| `Hal/<Arch>/Hal/` | 该架构私有细节（如 `HalPort.h`），见该目录 README |
| `Hal/Common/` | 跨 Arch HAL（`HalCapability`、`HalVideoStub`、DTB/RamFB 头） |
| `Core/` | 合流：`KernelMain`、模块表、PMM、`BOOT_INFO`（无 `Hal*` 实现） |
| `Core/Font.c` + `Theme.c` | Font 积木实现 `HalVideoDrawString*`；Theme 色板供 Gui |
| `Hal/X64/HalSerialGop.c` | `SCREEN_LOG=1` 时 boot 日志上滚到 FB |
| `Core/PhysicalMemory.c` | PMM：BOOT_INFO 自由区上的位图页分配 |
| `Core/Device.c` | 设备框架空壳（K5） |
| `Core/VirtualMemory.c` | 认领 EarlyIdentity；K13 `MapMmio` 高址 BAR |
| `Hal/X64/HalVideo.c` | LFB + K6 背缓冲 / Present |
| `Hal/X64/HalCpu.c` | K7：最小 GDT/IDT（不 sti） |
| `Core/Scheduler.c` | K8：协作壳（Init only） |
| `Core/Console.c` | K8：`ToyOS ready` + `Blocks>` 回显 |
| `Core/FileSystem.c` | K16：virtio-blk+FAT 探 `TOYOS.ID`（失败退 handoff） |
| `Core/FatProbe.c` | 薄 FAT 根目录扫描 |
| `Hal/X64/HalVirtioBlk.c` | legacy virtio-blk 读扇区（`HalBlock.h`） |
| `Core/Gui.c` | K17：XOR 光标 + 顶栏点击日志（`GuiPoll`） |
| `Hal/X64/HalPs2Mouse.c` | i8042 辅助口相对鼠标 |
| `Hal/X64/HalLapicTimer.c` | K18：LAPIC 周期 tick（`HalTimer.h`） |
| `Core/Scheduler.c` | K18：开 timer、Yield=`hlt` |
| `Core/Process.c` / `ElfLoad.c` / `FatFile.c` | K19：读跑 `HELLO.ELF`（int 0x80） |
| `User/X64/Hello.S` | 用户程序源；`User/X64/build.sh` → RootFs |
| `Core/Usb.c` | USB 模块胶水：MapMmio + HalXhci |
| `Hal/X64/HalXhci.c` | K14：xHCI 复位 + 端口 CCS（门面 `HalXhci.h`） |
| `Hal/X64/HalPs2Kbd.c` | K15：i8042 键盘 → ASCII（门面 `HalPs2Kbd.h`） |
| `Core/Network.c` | K11：PCI 扫 Network class（0x02） |
| `Core/Gui.c` | K12：最小桌面壳（底色/顶栏/标题） |

## 编译

```bash
cd ~/Blocks/Kernel
./build.sh x64|arm64|riscv   # → Build/<Arch>/Kernel.elf
./build.sh x64 SERIAL=0      # 编译期关掉 UART
./build.sh x64 SCREEN_LOG=1  # boot 日志画到屏幕（分通道 TOY_SCREEN_LOG_*）
```
