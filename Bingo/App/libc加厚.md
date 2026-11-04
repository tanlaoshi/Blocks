# libc 加厚 · Blocks Hosted

> **方言铁律**：实现 **`-std=c99 -pedantic`**；禁 GNU `__*` 类型预定义散落。  
> **约定**：无手测 → TG+JX。细表 [`LibcGaps.md`](LibcGaps.md)。

---

## ★ 当前与排队

> **当前 ★**：**H7 `hosted-stdlib-misc`** — `assert.h` / `bsearch` / `rand`/`srand`（C99）。  
> **本刀**：开干（H5/H6 需系统能力，暂后置）。  
> **排队**：H5 墙上钟 → H6 remove/rename → H8 signal。  
> **刚 TG**：H4 getenv/setenv；H2/H3；C99 头净化。

| # | 刀 | 状态 |
|---|-----|------|
| H1～H4 | printf 长度 / 三流 / scan / getenv | ✅ |
| H5 | 墙上钟 | 排队（需 RTC/syscall） |
| H6 | remove/rename | 排队（需 unlink syscall） |
| H7 | assert/bsearch/rand | ★ |
| H8 | signal/raise | 排队 |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | C99 铁律；H2～H4 TG；★→H7（H5/H6 等系统） |
