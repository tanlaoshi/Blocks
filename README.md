# OpenBox

ToyOS 新工作树（单仓）。现网对照：`~/ToyOS`。

```text
OpenBox/
  Boot/        # 仅独立 EFI → BOOTX64.EFI
  Kernel/          # 含各 Arch KernelEntry/KernelHandoff → Kernel.elf
  Runtime/
```

- **结构拍板**：`~/ToyOS/ToyKernel/Documents/开发/目录结构-ToyOSNew.md`
- **Boot 细则**：[`Boot/README.md`](Boot/README.md)
- **Boot / 接棒进度**：[`Boot迁移.md`](Boot迁移.md)（已接到 `KernelMain` 桩）
- **GitHub**：`git@github.com:tanlaoshi/OpenBox.git`

## 怎么开始

### Boot（仅 X64 EFI）

```bash
cd Boot && ./build.sh      # → Build/BOOTX64.EFI
```

Arm64/RiscV 入口在 **`Kernel/Hal/`**（链进 Kernel.elf），不在 `Boot/`。

### Kernel（接到 `KernelMain` 桩）

```bash
cd Kernel && ./build.sh x64|arm64|riscv   # → Build/<Arch>/Kernel.elf
```

工具链：X64 EFI→`Boot/EDK2`；交叉链→`Tools/Extract/`。详见 [`Boot迁移.md`](Boot迁移.md)。

### 尚未迁入

- Runtime → QEMU / 刷盘  
- `KernelMain` 之后的完整内核
