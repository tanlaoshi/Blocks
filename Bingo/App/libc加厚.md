# libc 加厚 · Blocks Hosted

> **方言铁律**：实现必须 **`-std=c99 -pedantic`**；禁止 GNU `__*` 类型预定义散落。  
> **树**：`Kernel/User/Library/Libc/`（**无** `User/crt/`）。ISO 按头分目录；`start/` 仅 `_start`/syscall/`call_main`（非 ISO）。  
> **命名**：公开 ISO 名；实现侧**全称 + 下划线**（可读；非 PascalCase）。  
> **可读**：分支铺平、契约注释；**不**再抽空门面层。  
> **约定**：无手测 → TG+JX。细表 [`LibcGaps.md`](LibcGaps.md)。  
> **CRT**：**1.9.6**。Review：[`Blocks-Hosted-libc-CodeReview-1105.md`](../../Archive/过程/审查/Blocks-Hosted-libc-CodeReview-1105.md)。

---

## ★ 当前与排队

> **当前 ★**：（空）功能柱 L1～L6 已收；1105 Code Review 已记档。  
> **排队**：无。  
> **刚收**：L6 + Review（功能可留；展览风按可读性保留；缺口补记）。

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
