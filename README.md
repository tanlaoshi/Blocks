# Blocks

ToyOS 新工作树（单仓）。现网对照：`~/ToyOS`。

```text
Blocks/
  Boot/
  Kernel/Build/
  Runtime/
  Tools/
```

- **Boot 细则**：[`Boot/README.md`](Boot/README.md)
- **GitHub**：`git@github.com:tanlaoshi/Blocks.git`

## 怎么开始

### Boot

```bash
cd ~/Blocks/Boot && ./build.sh   # → Boot/Build/BOOTX64.EFI
```

工具链见 `Tools/`（若有）。

### Runtime（已迁自 ToyImage）

```bash
cd ~/Blocks/Runtime && ./run.sh --headless   # 需先编好 Boot + Kernel
```

### Kernel（已迁入 + ABI 640）

```bash
cd ~/Blocks/Kernel && ./build.sh   # → Build/CodeA-HAL/X64/Kernel.elf
```
