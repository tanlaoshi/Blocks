# ISO C / libc 缺口表

> **口径**：**Blocks Hosted**（本系统宿主库）；对齐 freestanding 七头已齐。  
> **不做**：Linux 对标；`wchar`/`locale` 全量 / `complex`。  
> **活档**：[`libc加厚.md`](libc加厚.md)。**CRT**：**1.8.0+**。

## Freestanding 七头

全部 ✅（`float`/`iso646`/`limits`/`stdarg`/`stdbool`/`stddef`/`stdint`）。

## Blocks Hosted 进度

| 能力 | 状态 |
| --- | --- |
| `printf` 整数/字符串/`%f%e%g`/长度修饰 | H1 进行中→✅ |
| `stdin`/`stdout`/`stderr` | H2 |
| `fgets`/`sscanf` | H3 |
| `getenv` | H4（需 environ） |
| 墙上钟 | H5 |
| `remove`/`rename` | H6 |
| `assert`/`bsearch`/`rand` | H7 |
| `signal`/`raise` | H8 |

## 明确不做

`locale` / `wchar` / `complex` / musl / Linux syscall 号。
