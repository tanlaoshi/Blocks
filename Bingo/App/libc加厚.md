# libc 加厚

> **本柱活档**：只认本文首 ★。约定：无手测 → TG+JX 下一刀。

---

## ★ 当前与排队

> **当前 ★**：**PR6 `libc-setjmp`** — `setjmp.h` + x64 汇编。  
> **本刀**：开干；virt 可后置。  
> **排队**：PR7 time → PR8 math → PR9 atexit。  
> **刚 TG**：PR5 `libc-stdio-buf`。

| # | PR | 状态 |
|---|-----|------|
| 0–5 | …stdio-buf | ✅ |
| 6 | `libc-setjmp` | ★ |
| 7 | `libc-time` | 排队 |
| 8 | `libc-math` | 排队 |
| 9 | `libc-atexit` | 排队 |

---

## 归档

| PR | 状态 | 说明 |
|----|------|------|
| PR0～PR4 | ✅ | — |
| PR5 `libc-stdio-buf` | ✅ | 行缓冲 + `fflush`/`ungetc`；`stdio_rw.c` |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | **TG** PR5；★→PR6 |
| 2026-11-04 | **TG** PR0～PR4 |
