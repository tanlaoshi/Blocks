# libc 加厚 · Blocks Hosted

> **方言铁律**：实现必须 **`-std=c99 -pedantic`**；禁止 GNU `__*` 类型预定义散落。  
> **展览级**：原地写美（全称命名、分支铺平、契约注释）；**不**再抽空门面层。  
> **约定**：无手测 → TG+JX。细表 [`LibcGaps.md`](LibcGaps.md)。  
> **CRT**：**1.9.5** → 本柱收口抬版。

---

## ★ 当前与排队

> **当前 ★**：**L3 `stdio-clearerr-rewind`** — `clearerr` / `rewind`。  
> **排队**：L4 string 补缺 → L5 time 补缺 → L6 缺口表收口标版。  
> **刚 TG**：L2 malloc freelist。

| # | 刀 | 状态 |
|---|-----|------|
| H/X | Hosted 功能柱 | ✅ |
| R1～R6 | 展览级可读性 | ✅ |
| L1 | fopen `"w"` 截断语义 | ✅ |
| L2 | malloc freelist | ✅ |
| L3 | clearerr / rewind | ★ |
| L4 | memchr / strspn / strcspn / strpbrk | 排队 |
| L5 | localtime / mktime / difftime + strftime 星期 | 排队 |
| L6 | LibcGaps 已知缩水表 + CRT 标版 | 排队 |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | 功能柱 L1～L6；★→L1 |
| 2026-11-04 | 展览级可读性柱 R1～R6 |
| 2026-11-04 | P1 自查硬化；CRT 1.9.5 |
