# Blocks

**Blocks = 积木**：模块可自主拼接、替换。原则见 [`积木原则.md`](积木原则.md)。

```text
Blocks/
  Boot/        # X64 独立 EFI → BOOTX64.EFI
  Kernel/      # 三架构 KernelEntry / KernelHandoff → Kernel.elf
  Runtime/     # Esp / RootFs / Fw + run.sh（QEMU）
  Tools/       # 交叉工具链等
```

- **系统说明（权威文档：职责/原理/运行逻辑/依赖）**：[`Blocks系统说明.md`](Blocks系统说明.md)
- **积木原则**：[`积木原则.md`](积木原则.md)
- **调用链（Boot→shell）**：[`调用链.md`](调用链.md)
- **Boot 迁移**：[`Boot迁移.md`](Boot迁移.md)
- **Kernel 迁移 / ★**：[`Kernel迁移.md`](Kernel迁移.md)
- **Boot**：[`Boot/README.md`](Boot/README.md)
- **Kernel**：[`Kernel/README.md`](Kernel/README.md)
- **Runtime**：[`Runtime/README.md`](Runtime/README.md)

## 怎么开始

```bash
cd ~/Blocks/Boot && ./build.sh
cd ~/Blocks/Kernel && ./build.sh x64
cd ~/Blocks/Runtime && ./run.sh --headless
```

Arm64 / RiscV 入口在 `Kernel/Hal/<Arch>/`。交叉链：`Tools/Extract/`。
