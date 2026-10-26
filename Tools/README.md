# Tools

交叉工具链等本机构建依赖。参考旧仓 `ToyKernel/Tools`，路径按 `Kernel/Scripts` 修正。

| 路径 | 说明 |
| ---- | ---- |
| `xpack-*/` | 解压后的 xPack 等（**不进 git**） |
| `Tarballs/` | 可选缓存包（不进 git） |

X64 的 Boot / Kernel 用系统 `gcc`，不读这里。Arm64 / RiscV 时，`Kernel/Makefile` 会找：

1. `Tools/xpack-aarch64-none-elf-gcc-*/` 或 `xpack-riscv-none-elf-gcc-*/`
2. 否则 PATH 上的 `aarch64-linux-gnu-gcc` / `riscv64-linux-gnu-gcc`

换电脑时如何只拷这份工具链（要保留符号链接），见 [`Archive/同步.md`](../Archive/同步.md)。目标目录是 `~/Blocks/Tools/`（`xpack-*` 直接放在此层，不再套 `Extract/`）。
