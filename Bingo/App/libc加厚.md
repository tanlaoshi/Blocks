# libc 加厚

> **本柱活档**：只认本文首 ★（勿跟 1104 Duo 刀）。  
> **定名**：底库 **`libc.a`**（`-lc`）。缺口真源 [`LibcGaps.md`](LibcGaps.md)。  
> **约定**：无需手测的刀 → 编过即 **TG** 并 **JX** 下一刀。

---

## ★ 当前与排队

> **当前 ★**：**PR5 `libc-stdio-buf`** — 行/全缓冲 + `fflush` / `ungetc`。  
> **本刀**：开干；宽字符流不做。  
> **排队**：PR6 setjmp → PR7 time → PR8 math → PR9 atexit。  
> **刚 TG**：PR4 `libc-stdio-fmt`。

| # | PR | 状态 |
|---|-----|------|
| 0–4 | rename…stdio-fmt | ✅ |
| 5 | `libc-stdio-buf` | ★ |
| 6 | `libc-setjmp` | 排队 |
| 7 | `libc-time` | 排队 |
| 8 | `libc-math` | 排队 |
| 9 | `libc-atexit` | 排队 |

---

## 归档

| PR | 状态 | 说明 |
|----|------|------|
| PR0～PR3 | ✅ | rename / headers / string / parse |
| PR4 `libc-stdio-fmt` | ✅ | `stdio_fmt.c`：`fprintf`/`fputs`/`fputc`/`putchar` |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | **TG** PR4；★→PR5 |
| 2026-11-04 | **TG** PR3 / PR2 / PR1 / PR0 |
