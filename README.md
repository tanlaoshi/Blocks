# Blocks

ToyOS 新工作树（单仓）。现网对照：`~/ToyOS`。

```text
Blocks/
  Boot/
  Kernel/
  Runtime/
  Tools/
  Documents/   ← 项目 / 规范 / 手册 / 过程 / 柱规格
```

- **文档地图**：[`Documents/README.md`](Documents/README.md)
- **从 OVMF 逐段学懂**：[`原则与学习记录.md`](原则与学习记录.md)
- **Boot 细则**：[`Boot/README.md`](Boot/README.md)
- **换一台电脑**：[`同步.md`](同步.md)（拉分支 `working`，不要拉 `main`）
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
