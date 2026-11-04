# libc 加厚 · Blocks Hosted

> **本柱活档**：只认本文首 ★。  
> **口径**：C99 **Blocks Hosted**；对外小写蛇形。  
> **方言铁律**：`-std=c99 -pedantic`；禁止 GNU 方言 / `__builtin_*` / `__INT*_TYPE__`（[`身份.md`](身份.md)）。  
> **不做**：Linux 对标；`wchar`/`locale` 全量 / `complex`。  
> **约定**：无手测 → TG+JX。细表 [`LibcGaps.md`](LibcGaps.md)。

---

## ★ 当前与排队

> **当前 ★**：**H4 `hosted-getenv`** — `environ` + `getenv`/`setenv`。  
> **本刀**：开干。  
> **排队**：H5 墙上钟 → H6 unlink/rename → H7 stdlib-misc → H8 signal。  
> **刚 TG**：H2 streams + H3 scan；头文件去 GNU 预定义（纯 C99）。

| # | 刀 | 范围 | 状态 |
|---|-----|------|------|
| — | PR0～PR11 / H1 | freestanding + printf 长度 | ✅ |
| H2 | `hosted-stdio-streams` | `stdin`/`stdout`/`stderr` | ✅ |
| H3 | `hosted-stdio-scan` | `fgetc`/`fgets`/`getchar`/`sscanf` | ✅ |
| H4 | `hosted-getenv` | `environ`/`getenv`/`setenv` | ★ |
| H5 | `hosted-time-wall` | 墙上钟 | 排队 |
| H6 | `hosted-file-maintain` | `remove`/`rename` | 排队 |
| H7 | `hosted-stdlib-misc` | `assert`/`bsearch`/`rand` | 排队 |
| H8 | `hosted-signal` | `raise` + 加厚 | 排队 |

---

## 归档

| 刀 | 状态 | 说明 |
|----|------|------|
| H1 | ✅ | printf `%l`/`%ll`/`%z`/`%p`/`%i`/`%o` |
| H2 | ✅ | 三流 + `FF_STATIC` |
| H3 | ✅ | `fgets`/`sscanf` 最小 `%d%u%s` |
| C99 头 | ✅ | `float.h`/`stdint.h` 去 `__*` 预定义 |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | 钉 C99 方言铁律；`float`/`stdint` 纯字面量；**TG** H2+H3；★→H4 |
| 2026-11-04 | 柱改 Blocks Hosted；H1 TG |
