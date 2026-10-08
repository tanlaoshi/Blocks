# Tools

Boot / Kernel **共用**的本机构建依赖。

| 路径 | 说明 |
| ---- | ---- |
| `Extract/` | 解压后的交叉编译器（**不进 git**） |
| `Tarballs/` | 可选下载缓存（不进 git） |

## 交叉工具链（Arm64 + RiscV）

| 目录 | 用途 |
| ---- | ---- |
| `Extract/xpack-aarch64-none-elf-gcc-*/` | Arm64 Kernel |
| `Extract/xpack-riscv-none-elf-gcc-*/` | RiscV Kernel |

X64：Boot 用 `Boot/EDK2`，Kernel 多用本机 `gcc`。

查找顺序：

1. `Blocks/Tools/Extract/xpack-*-gcc-*/bin/`
2. 否则 `PATH` 上的系统交叉器

可用环境变量 `TOOLS_ROOT` 覆盖 Extract 根目录。

## 首次放入

推荐：AArch64 **13.2.1-1.1**，RISC-V **13.2.0-2**（xPack）。

```bash
mkdir -p ~/Blocks/Tools/Extract ~/Blocks/Tools/Tarballs
# 将解压后的 xpack-*-gcc-* 目录放到 Extract/ 下
```

工具链应落在本树 `Tools/Extract/` 内。
