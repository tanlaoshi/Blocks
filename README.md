# OpenBox

ToyOS 新工作树（单仓）。现网对照：`~/ToyOS`（先不动）。

```text
OpenBox/
  Boot/       # 引导（X64 / Arm64 / RiscV）；X64 下含 EDK2 工具包
  Kernel/     # 内核与用户态；产物 Kernel/Build/
  Runtime/    # 可运行镜像 / RootFs；无顶层 Build
```

- **结构方案（旧仓对照文）**：`~/ToyOS/ToyKernel/Documents/开发/目录结构-ToyOSNew.md`  
- **GitHub**：`git@github.com:tanlaoshi/OpenBox.git`（单仓；此后不再拆多仓）  
- **EDK2**：将来进 `Boot/X64/EDK2/`，**不带**独立 `.git`  
- **现在**：空骨架。确认清单前不搬代码；**文档不从旧仓迁移，从新写。**  
- **迁移方式**：逐步、小刀；每刀你 **code review** 通过后再开下一刀。

## 怎么开始（占位）

迁入代码并改脚本后：

1. 编 Boot（X64）→ `Boot/Build/…`
2. 编 Kernel → `Kernel/Build/…`
3. 同步到 Runtime → 跑 QEMU / 刷盘

（具体命令随迁移刀补进本 README。）
