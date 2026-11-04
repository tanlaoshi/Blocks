# libc 加厚 · Blocks Hosted

> **方言铁律**：实现必须 **`-std=c99 -pedantic`**；禁止 GNU `__*` 类型预定义散落。  
> **约定**：无手测 → TG+JX。细表 [`LibcGaps.md`](LibcGaps.md)。  
> **CRT**：**1.9.0**。

---

## ★ 当前与排队

> **当前 ★**：**H5 `hosted-time-wall`** — 墙上钟托 `time`（需系统 RTC/epoch；本刀先探能力再落）。  
> **排队**：H6 remove/rename（需 unlink syscall）。  
> **刚 TG**：H7 assert/bsearch/rand；H8 raise/kill；CRT → **1.9.0**。

| # | 刀 | 状态 |
|---|-----|------|
| H1～H4 | printf长度 / 三流 / scan / getenv | ✅ |
| H7 | assert/bsearch/rand | ✅ |
| H8 | raise + kill 包装 | ✅ |
| H5 | 墙上钟 | ★ |
| H6 | remove/rename | 排队 |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | **钉 C99**；净化 float/stdint；H1～H4/H7/H8 TG；CRT 1.9.0；★→H5 |
