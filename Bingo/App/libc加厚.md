# libc 加厚

> **本柱活档**：只认本文首 ★。  
> **口径**：对外小写蛇形；**C99 freestanding**；**非** hosted。  
> **约定**：无手测 → TG+JX。细表 [`LibcGaps.md`](LibcGaps.md)。  
> **CRT**：**1.8.0**。

---

## ★ 当前与排队

> **当前 ★**：**PR12 `libc-printf-length`** — `%ld`/`%lu`/`%lld`/`%llu`/`%zu`/`%p`（可选 `%i`/`%o`）。  
> **本刀**：开干。  
> **排队**：PR13 scan → PR14 streams → PR15 stdlib-misc。  
> **刚 TG**：PR11 `libc-printf-float`（`%f`/`%e`/`%g` + `.prec`）。

| # | PR | 范围 | 状态 |
|---|-----|------|------|
| 0～9 | 首轮加厚 | — | ✅ |
| 10 | freestanding 七头 | `float.h`/`iso646.h` | ✅ |
| 11 | `libc-printf-float` | `%f`/`%e`/`%g` | ✅ |
| 12 | `libc-printf-length` | 长度修饰 / `%p` | ★ |
| 13 | `libc-stdio-scan` | `fgets`/`sscanf` 最小 | 排队 |
| 14 | `libc-stdio-streams` | stdin/out/err | 排队 |
| 15 | `libc-stdlib-misc` | `bsearch`/`rand` | 排队 |

---

## 归档

| PR | 状态 | 说明 |
|----|------|------|
| PR0～PR9 | ✅ | CRT 1.7.0 |
| PR10 | ✅ | freestanding 七头；CRT 1.8.0 |
| PR11 | ✅ | `printf_float.c`；教学 `%f%e%g` |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | **TG** PR11；★→PR12 |
| 2026-11-04 | **TG** PR10；拆 PR11～15；对照 printf 函数差 |
