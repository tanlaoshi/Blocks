# OpenBox

ToyOS 新工作树（单仓）。现网对照：`~/ToyOS`。

```text
OpenBox/
  Boot/
    X64/Build/     # BOOTX64.EFI
    Arm64/Build/   # Boot_asm.o Boot.o
    RiscV/Build/   # 同上
  Kernel/Build/
  Runtime/
```

- **结构拍板**：`~/ToyOS/ToyKernel/Documents/开发/目录结构-ToyOSNew.md`
- **Boot 细则**：[`Boot/README.md`](Boot/README.md)
- **GitHub**：`git@github.com:tanlaoshi/OpenBox.git`

## 怎么开始

### Boot（可编）

```bash
cd Boot/X64 && ./build.sh      # → X64/Build/BOOTX64.EFI
cd Boot/Arm64 && ./build.sh    # → Arm64/Build/*.o → KernelMain(BOOT_INFO*)
cd Boot/RiscV && ./build.sh
```

工具链见 `Tools/README.md`。

### 尚未迁入

2. Kernel 实现 `KernelMain(const BOOT_INFO *)` 并链各架构 Boot `*.o`
3. Runtime → QEMU / 刷盘
