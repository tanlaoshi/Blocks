# Tools

Boot / Kernel **共用**的本机构建依赖（与架构无关地放在这里，两边都来取）。

| 路径 | 说明 |
| ---- | ---- |
| `Extract/` | 解压后的 xPack 交叉编译器（**不进 git**） |
| `Tarballs/` | 可选下载缓存（不进 git） |

## 交叉工具链（Arm64 + RiscV）

| 目录 | 用途 |
| ---- | ---- |
| `Extract/xpack-aarch64-none-elf-gcc-*/` | Arm64：Boot + Kernel 同一套 |
| `Extract/xpack-riscv-none-elf-gcc-*/` | RiscV：Boot + Kernel 同一套 |

X64 不依赖这里：Boot 用 `Boot/X64/EDK2`，Kernel 多用本机 `gcc`。

`Boot/Arm64/build.sh`、`Boot/RiscV/build.sh`（以及后续 Kernel 构建）默认找：

1. `OpenBox/Tools/Extract/xpack-*-gcc-*/bin/`
2. 否则 `PATH` 上的系统交叉器

可用环境变量 `TOOLS_ROOT` 覆盖 Extract 根目录。

## 首次放入（本树内，勿链到 OpenBox 外）

推荐版本与旧 ToyKernel 一致：AArch64 **13.2.1-1.1**，RISC-V **13.2.0-2**。

```bash
mkdir -p ~/OpenBox/Tools/Tarballs ~/OpenBox/Tools/Extract
cd ~/OpenBox/Tools

curl -L -o Tarballs/riscv-gnu.tar.gz \
  'https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v13.2.0-2/xpack-riscv-none-elf-gcc-13.2.0-2-linux-x64.tar.gz'
tar -xzf Tarballs/riscv-gnu.tar.gz -C Extract

curl -L -o Tarballs/arm-gnu.tar.gz \
  'https://github.com/xpack-dev-tools/aarch64-none-elf-gcc-xpack/releases/download/v13.2.1-1.1/xpack-aarch64-none-elf-gcc-13.2.1-1.1-linux-x64.tar.gz'
tar -xzf Tarballs/arm-gnu.tar.gz -C Extract
```

从旧对照仓迁入时请 **复制进本树**（不要软链到仓外）：

```bash
cp -a /path/to/ToyKernel/Tools/Extract/xpack-aarch64-none-elf-gcc-* ~/OpenBox/Tools/Extract/
cp -a /path/to/ToyKernel/Tools/Extract/xpack-riscv-none-elf-gcc-* ~/OpenBox/Tools/Extract/
```

验收：

```bash
ls Extract/xpack-aarch64-none-elf-gcc-*/bin/aarch64-none-elf-gcc
ls Extract/xpack-riscv-none-elf-gcc-*/bin/riscv-none-elf-gcc
cd ~/OpenBox/Boot/Arm64 && ./build.sh
cd ~/OpenBox/Boot/RiscV && ./build.sh
```
