# libc 加厚 · Blocks Hosted

> **本柱活档**：只认本文首 ★。  
> **口径**：C99 **Blocks Hosted**（本系统上的宿主库语义）；对外小写蛇形。  
> **不做**：Linux syscall 号 / glibc 对标；`wchar`/`locale` 全量 / `complex`。  
> **约定**：无手测 → 自测编过即 **TG+JX**。细表 [`LibcGaps.md`](LibcGaps.md)。  
> **CRT**：**1.8.0**（本柱涨版见各刀）。

---

## ★ 当前与排队

> **当前 ★**：**H1 `hosted-printf-length`** — `%ld`/`%lld`/`%zu`/`%p`/`%i`/`%o`。  
> **本刀**：开干。  
> **排队**：H2 streams → H3 scan → H4 getenv → H5 墙上钟 → H6 unlink/rename → H7 stdlib-misc → H8 signal。  
> **刚 TG**：PR11 float 格式；柱目标改为 Blocks Hosted。

| # | 刀 | 范围 | 状态 |
|---|-----|------|------|
| — | PR0～PR11 | freestanding + 首轮加厚 | ✅ |
| H1 | `hosted-printf-length` | 长度修饰 / `%p`/`%i`/`%o` | ★ |
| H2 | `hosted-stdio-streams` | `stdin`/`stdout`/`stderr` | 排队 |
| H3 | `hosted-stdio-scan` | `fgets`/`getchar`/`sscanf` 最小 | 排队 |
| H4 | `hosted-getenv` | `environ` + `getenv`/`setenv`（CRT 先） | 排队 |
| H5 | `hosted-time-wall` | 墙上钟托 `time`（需系统能力则同刀或拆） | 排队 |
| H6 | `hosted-file-maintain` | `remove`/`rename`（+ syscall 若缺） | 排队 |
| H7 | `hosted-stdlib-misc` | `assert.h`/`bsearch`/`rand`/`srand` | 排队 |
| H8 | `hosted-signal` | `raise` + 信号语义加厚 | 排队 |

---

## 归档

| 刀 | 状态 | 说明 |
|----|------|------|
| PR0～PR11 | ✅ | 见修订史；CRT 1.8.0 |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | 柱改 **Blocks Hosted**；拆 H1～H8；★→H1；开干 |
| 2026-11-04 | PR0～PR11 收官（freestanding + 加厚） |
