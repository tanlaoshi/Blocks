# ISO C / libc 缺口表

> **方言**：实现 **C99**（`-std=c99 -pedantic`）。  
> **目标**：**Blocks Hosted**（非 Linux 对标）。不做 `wchar`/`locale`/`complex`。  
> **CRT**：**1.9.5**。活档 [`libc加厚.md`](libc加厚.md)。

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

## 表外加厚

| 项 | 状态 |
| --- | --- |
| virt setjmp（aarch64/riscv） | ✅ |
| exec 继承 environ | ✅ |
| scanf 加厚（scanf/fscanf + `%x/%o/%f/%c/%l*`） | ✅ |
| math 加厚（sin/cos/tan/atan/atan2/exp/log/log10） | ✅ |

仍不做：`wchar`/`locale`/`complex`、完整 ISO Hosted 全库。
