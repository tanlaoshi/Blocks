# Kernel

凡进 **`Kernel.elf`** 的源码都在本树（含各架构开机入口）。

顶层 **`Boot/`** 只放独立 EFI（现仅 `Boot` → `BOOTX64.EFI`）。

## 编译

```bash
cd ~/OpenBox/Kernel
./build.sh x64|arm64|riscv   # → Build/<Arch>/Kernel.elf
```

```text
三架构：
  KernelEntry → KernelHandoff → KernelMain（桩）
X64 另有：EarlyIdentity（4GiB 恒等）；EFI 在 Boot
```

| 路径 | 说明 |
| ---- | ---- |
| `Hal/X64/KernelEntry.S` `KernelHandoff.c` `EarlyIdentity.c` | UEFI 跳入后接棒 |
| `Hal/Arm64/KernelEntry.S` `KernelHandoff.c` | virt `-kernel` 入口 |
| `Hal/RiscV/KernelEntry.S` `KernelHandoff.c` `SmpStub.c` | OpenSBI 入口 |
| `Core/BootInfo.c` `KernelMain.c` | 清单 + 桩 |
| `Include/Core/IdentityMap.h` | 早期恒等 4GiB |

进度：[`../Boot迁移.md`](../Boot迁移.md)。
