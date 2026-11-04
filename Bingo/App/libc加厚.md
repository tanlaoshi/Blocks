# libc 加厚

> **本柱活档**：只认本文首 ★。  
> **口径**：对外 Linux/ISO **小写蛇形**；**对齐 C99 freestanding**，**不做** hosted/Linux 对标。  
> **约定**：无手测 → TG+JX。缺口细表 [`LibcGaps.md`](LibcGaps.md)。  
> **CRT**：**1.8.0**。

---

## ★ 当前与排队

> **当前 ★**：**PR11 `libc-printf-float`** — `printf` 族加 `%f`/`%e`/`%g`（教学精度即可）。  
> **本刀**：开干前先认 LibcGaps「printf 差多少」；无手测。  
> **排队**：PR12 长度修饰 → PR13 `fgets`/`sscanf` → …  
> **刚 TG**：PR10 freestanding 七头（`float.h`/`iso646.h`）；CRT → **1.8.0**。

| # | PR | 范围 | 状态 |
|---|-----|------|------|
| 0～9 | rename…atexit | 见归档 | ✅ |
| 10 | `libc-freestanding-headers` | `float.h` + `iso646.h`；钉齐七头 | ✅ |
| 11 | `libc-printf-float` | `%f` `%e` `%g` | ★ |
| 12 | `libc-printf-length` | `%ld`/`%lu`/`%lld`/`%llu`/`%zu`/`%p`；可选 `%i`/`%o` | 排队 |
| 13 | `libc-stdio-scan` | `fgets`/`getchar`；`sscanf` 最小子集 | 排队 |
| 14 | `libc-stdio-streams` | `stdin`/`stdout`/`stderr` 对象（可薄） | 排队 |
| 15 | `libc-stdlib-misc` | `bsearch`/`rand`/`srand`（env 另刀） | 排队 |

**不做**：hosted 全表；`locale`/`wchar`；musl；Linux syscall 号。

---

## 归档

| PR | 状态 | 说明 |
|----|------|------|
| PR0～PR9 | ✅ | 首轮加厚；CRT 1.7.0 |
| PR10 | ✅ | freestanding 七头齐；CRT 1.8.0 |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | **TG** PR10；拆 PR11～15（printf/stdio 函数加厚）；★→PR11 |
| 2026-11-04 | PR0～PR9 收官；明确非 hosted |
