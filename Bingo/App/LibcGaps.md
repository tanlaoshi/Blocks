# ISO C / libc 缺口表

> **方言**：实现 **C99**（`-std=c99 -pedantic`）。  
> **目标**：**Blocks Hosted**（非 Linux 对标）。不做 `wchar`/`locale`/`complex`。  
> **CRT**：**1.9.0**。活档 [`libc加厚.md`](libc加厚.md)。

## Freestanding 七头

全部 ✅（`float`/`iso646`/`limits`/`stdarg`/`stdbool`/`stddef`/`stdint`；字面量、无 GNU `__INT*_TYPE__`）。

## Blocks Hosted 进度

| 项 | 状态 |
| --- | --- |
| printf 长度/`%f` | ✅ |
| stdin/out/err | ✅ |
| fgets/sscanf 最小 | ✅ |
| getenv/setenv | ✅ CRT 表 |
| assert/bsearch/rand | ✅ |
| raise/kill | ✅ 教学信号 |
| 墙上钟 | ★ H5（需系统） |
| remove/rename | H6（需 unlink syscall） |

## 仍缺（非本柱优先）

math 加厚、`scanf` 全族、virt setjmp、exec 继承 environ。
