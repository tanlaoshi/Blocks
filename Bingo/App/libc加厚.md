# libc 加厚

> **本柱活档**：只认本文首 ★。约定：无手测 → TG+JX 下一刀。

---

## ★ 当前与排队

> **当前 ★**：**PR7 `libc-time`** — 薄 `time.h`（`clock`/`time` 垫 `clock_ms`）。  
> **本刀**：开干。  
> **排队**：PR8 math → PR9 atexit。  
> **刚 TG**：PR6 `libc-setjmp`（x86_64）。

| # | PR | 状态 |
|---|-----|------|
| 0–6 | …setjmp | ✅ |
| 7 | `libc-time` | ★ |
| 8 | `libc-math` | 排队 |
| 9 | `libc-atexit` | 排队 |

---

## 归档

| PR | 状态 | 说明 |
|----|------|------|
| PR0～PR5 | ✅ | — |
| PR6 `libc-setjmp` | ✅ | `setjmp_x86_64.S`；virt 后置 |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | **TG** PR6；★→PR7 |
| 2026-11-04 | **TG** PR0～PR5 |
