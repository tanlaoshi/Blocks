# 单仓编 Kernel（BOX-7）

> **验收**：只要有本仓 `ToyKernel` + 交叉/本机工具链（`Tools/Extract` 或系统 gcc），即可 `./build.sh`。  
> **不需要**旁挂 EDK2、ToyBoot 源码、ToyImage 才能完成**编译**。

## 步骤

```bash
cd /path/to/ToyKernel          # 例如 $BLOCKS_ROOT/ToyKernel 或任意拷贝
# 工具链：已有 Tools/Extract 即可；否则按 Tools/README 拉取
./build.sh                     # 默认 x86_64 → Build/HAL/X64/Kernel.elf（单仓）
# ./build.sh arm64 LWIP=0
# ./build.sh riscv LWIP=0
# 旁有 ToyOS 树根时产物在 $BLOCKS_ROOT/Build/ToyKernel/
```

- 若旁边有 `../ToyImage/RootFs/…`：编完会同步 `Kernel.elf`（便利，非硬依赖）。  
- 无 ToyImage：打印 `note: no ../ToyImage/... skip`，**仍算成功**。

## 与 Boot / 镜像的关系（运行时，非编译）

| 产物 | 谁提供 | 说明 |
| ---- | ------ | ---- |
| `Kernel.elf` | 本仓 `./build.sh` | 本刀验收对象 |
| `BOOTX64.EFI` | `$BLOCKS_ROOT/ToyBoot` + `$BLOCKS_ROOT/EDK2` | BOX-6；加载 Kernel |
| RootFs / QEMU | `$BLOCKS_ROOT/ToyImage` | 把 `Kernel.elf` 放进 `RootFs/X64/` 后 `smoke` |

X64 交接包是两份内容相同的 `UefiBootConfig.h`：`Boot/BootPkg/` 与 `BringUp/X64/`。内核编译包含自己这份，不 `#include` Boot 树，才能单独编出 `Kernel.elf`。`BOOT_INFO` 仍是内核自己的、给 `KernelMain` 用的结构。

## 自检（曾用）

```bash
# 任意目录，仅本仓 + Extract 链接
rsync -a --exclude Build --exclude Tools/Extract …/ToyKernel/ /tmp/toykernel-solo/
ln -sfn …/Tools/Extract /tmp/toykernel-solo/Tools/Extract
cd /tmp/toykernel-solo && ./build.sh x86_64
# → Build/HAL/X64/Kernel.elf；无 ../ToyImage 时 skip 同步
```
