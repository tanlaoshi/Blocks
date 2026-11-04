# libc 加厚 · Blocks Hosted

> **方言铁律**：实现必须 **`-std=c99 -pedantic`**；禁止 GNU `__*` 类型预定义散落。  
> **约定**：无手测 → TG+JX。细表 [`LibcGaps.md`](LibcGaps.md)。  
> **CRT**：**1.9.1**。

---

## ★ 当前与排队

> **当前 ★**：**H6 `hosted-remove-rename`** — `remove`/`rename`（需 unlink/rename syscall）。  
> **排队**：（空）  
> **刚 TG**：H5 墙上钟 — `SYS_TIME_WALL=701` + CRT `time()`；CRT → **1.9.1**。

| # | 刀 | 状态 |
|---|-----|------|
| H1～H4 | printf长度 / 三流 / scan / getenv | ✅ |
| H7 | assert/bsearch/rand | ✅ |
| H8 | raise + kill 包装 | ✅ |
| H5 | 墙上钟 `SYS_TIME_WALL` | ✅ |
| H6 | remove/rename | ★ |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | H5：`SYS_TIME_WALL` + `time()`；CRT 1.9.1；★→H6 |
| 2026-11-04 | **钉 C99**；净化 float/stdint；H1～H4/H7/H8 TG；CRT 1.9.0；★→H5 |
