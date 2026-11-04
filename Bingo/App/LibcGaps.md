# ISO C / libc 缺口表

> **方言**：实现 **C99**（`-std=c99 -pedantic`）。  
> **目标**：**Blocks Hosted**（非 Linux 对标）。不做 `wchar`/`locale`/`complex`。  
> **CRT**：**1.9.2**。活档 [`libc加厚.md`](libc加厚.md)。

## Freestanding 七头

全部 ✅。

## Blocks Hosted 主列

| 项 | 状态 |
| --- | --- |
| printf 长度/`%f` | ✅ |
| stdin/out/err | ✅ |
| fgets/sscanf 最小 | ✅ |
| getenv/setenv | ✅ |
| assert/bsearch/rand | ✅ |
| raise/kill | ✅ |
| 墙上钟 | ✅ |
| remove/rename | ✅ |

## 表外加厚（本轮）

| 项 | 状态 |
| --- | --- |
| virt setjmp（aarch64/riscv） | ★ X1 |
| exec 继承 environ | X2 |
| scanf 加厚（scanf/fscanf + 常用转换） | X3 |
| math 加厚（trig/exp/log） | X4 |

仍不做：`wchar`/`locale`/`complex`、完整 ISO Hosted 全库。
