# libc 加厚

> **本柱活档**：只认本文首 ★（勿跟 1104 Duo 刀）。  
> **定名**：底库 **`libc.a`**（`-lc`）。缺口真源 [`LibcGaps.md`](LibcGaps.md)。  
> **约定**：无需手测的刀 → 编过即 **TG** 并 **JX** 下一刀。  
> **CRT**：**1.7.0**。

---

## ★ 当前与排队

> **当前 ★**：（空）本柱 PR0～PR9 已收。  
> **排队**：（空）后置见 LibcGaps（`getenv`/`fscanf`/virt setjmp）。  
> **刚 TG**：PR9 `libc-atexit`；CRT → **1.7.0**。

| # | PR | 范围 | 状态 |
|---|-----|------|------|
| 0 | `libc-rename` | `libBlocks`→`libc.a` | ✅ |
| 1 | `libc-headers` | `stdbool`/`stdint`/`ctype` | ✅ |
| 2 | `libc-string` | `strerror`/`strtok`/`strrchr`/`strncat` | ✅ |
| 3 | `libc-stdlib-parse` | `strtol`/`strtoul`/`strtod` | ✅ |
| 4 | `libc-stdio-fmt` | `fprintf`/`fputs`/`fputc`/`putchar` | ✅ |
| 5 | `libc-stdio-buf` | 行缓冲 + `fflush`/`ungetc` | ✅ |
| 6 | `libc-setjmp` | x86_64 `setjmp`/`longjmp` | ✅ |
| 7 | `libc-time` | 薄 `time.h` | ✅ |
| 8 | `libc-math` | 裁剪 `math.h` | ✅ |
| 9 | `libc-atexit` | `atexit`/`abort`；`exit` 跑钩子 | ✅ |

**不做（本柱）**：`locale`/`wchar`；musl；假 POSIX；Linux syscall 号。

---

## 归档

| PR | 状态 | 说明 |
|----|------|------|
| PR0～PR9 | ✅ | 加厚柱收官；CRT 1.7.0 |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | **TG** PR7～PR9；★ 空；CRT 1.7.0；柱收官 |
| 2026-11-04 | **TG** PR0～PR6；无手测连刀 |
