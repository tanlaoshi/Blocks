# Kernel

现网能力核（自 `ToyOS/ToyKernel` 迁入）。Boot 在旁挂 `../Boot`。

## 已落地

| 刀 | 内容 |
| -- | ---- |
| PR-K-abi-640 | X64 `BootHandoff` **640** + `OsIdSeen` |
| PR-K-entry-split | X64 `KernelEntry.S` + `KernelHandoff.c` |
| PR-K-early-4g | X64 `EarlyIdentity` 4GiB + VMM/PMM 对齐 |
| PR-K-arch3 | Arm64/RiscV：`Startup*` → `KernelEntry` / `KernelHandoff`（与 X64 同名合流） |

三架构上电合流：

```text
Hal/<Arch>/KernelEntry → KernelHandoff → KernelMain
```

## 编译

```bash
cd ~/Blocks/Kernel
./build.sh              # x64 → Build/CodeA-HAL/X64/Kernel.elf
./build.sh arm64        # → Build/CodeA-HAL/Arm64/Kernel.elf
./build.sh riscv        # → Build/CodeA-HAL/RiscV/Kernel.elf
```

交叉链：`../Tools/Extract/xpack-*`。

## 手测（X64 桌面）

```bash
cd ~/Blocks/Runtime && ./run.sh --headless
```
