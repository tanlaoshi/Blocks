# libc 加厚 · Blocks Hosted

> **方言铁律**：实现必须 **`-std=c99 -pedantic`**；禁止 GNU `__*` 类型预定义散落。  
> **展览级**：原地写美（全称命名、分支铺平、契约注释）；**不**再抽空门面层。  
> **约定**：无手测 → TG+JX。细表 [`LibcGaps.md`](LibcGaps.md)。  
> **CRT**：**1.9.6**。

---

## ★ 当前与排队

> **当前 ★**：（空）功能柱 L1～L6 已收。  
> **排队**：无。  
> **刚 TG**：L6 缺口表收口 + CRT 1.9.6。

| # | 刀 | 状态 |
|---|-----|------|
| H/X | Hosted 功能柱 | ✅ |
| R1～R6 | 展览级可读性 | ✅ |
| L1 | fopen `"w"` 截断语义 | ✅ |
| L2 | malloc freelist | ✅ |
| L3 | clearerr / rewind | ✅ |
| L4 | memchr / strspn / strcspn / strpbrk | ✅ |
| L5 | localtime / mktime / difftime + strftime 星期 | ✅ |
| L6 | LibcGaps 已知缩水表 + CRT 标版 | ✅ |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | 功能柱 L1～L6 收口；CRT 1.9.6 |
| 2026-11-04 | 展览级可读性柱 R1～R6 |
| 2026-11-04 | P1 自查硬化；CRT 1.9.5 |
