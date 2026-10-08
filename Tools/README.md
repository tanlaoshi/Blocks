# Tools

交叉工具链等本机构建依赖。参考旧仓 `ToyKernel/Tools`，路径按 OpenBox 修正。

| 路径 | 说明 |
| ---- | ---- |
| `Extract/` | 解压后的 xPack 等（**不进 git**） |
| `Tarballs/` | 可选缓存包（不进 git） |

Arm64 / RiscV Boot 编 `.o` 时，`Boot/*/build.sh` 会找：

1. `Tools/Extract/xpack-aarch64-none-elf-gcc-*/` 或 `xpack-riscv-none-elf-gcc-*/`
2. 否则 PATH 上的 `aarch64-*-gcc` / `riscv*-gcc`

首次可从旧对照仓拷入（只拷一次到本树，之后不再引用旧路径）：

```bash
mkdir -p ~/OpenBox/Tools/Extract
cp -a ~/ToyOS/ToyKernel/Tools/Extract/xpack-aarch64-none-elf-gcc-* ~/OpenBox/Tools/Extract/
cp -a ~/ToyOS/ToyKernel/Tools/Extract/xpack-riscv-none-elf-gcc-* ~/OpenBox/Tools/Extract/
```
