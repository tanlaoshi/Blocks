# Blocks

**Blocks = 积木**：模块可自主拼接、替换。原则见 [`积木原则.md`](积木原则.md)。

```text
Blocks/
  Boot/        # X64 独立 EFI → BOOTX64.EFI
  Kernel/      # 三架构 KernelEntry / KernelHandoff → Kernel.elf
  Runtime/     # 系统盘布局、QEMU / 刷盘脚本（建设中）
  Tools/       # 交叉工具链等
```

- **积木原则**：[`积木原则.md`](积木原则.md)
- **Boot 细则**：[`Boot/README.md`](Boot/README.md)
- **Kernel 细则**：[`Kernel/README.md`](Kernel/README.md)

## 怎么开始

```bash
cd ~/Blocks/Boot && ./build.sh                 # → Build/BOOTX64.EFI
cd ~/Blocks/Kernel && ./build.sh x64|arm64|riscv   # → Build/<Arch>/Kernel.elf
```

Arm64 / RiscV 入口在 `Kernel/Hal/<Arch>/`，不在 `Boot/`。  
交叉链：`Tools/Extract/`；X64 Boot 用 `Boot/EDK2`。
