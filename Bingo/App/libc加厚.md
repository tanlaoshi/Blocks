# libc 加厚 · Blocks Hosted

> **方言铁律**：实现必须 **`-std=c99 -pedantic`**；禁止 GNU `__*` 类型预定义散落。  
> **展览级**：原地写美（全称命名、分支铺平、契约注释）；**不**再抽空门面层。  
> **约定**：无手测 → TG+JX。细表 [`LibcGaps.md`](LibcGaps.md)。  
> **CRT**：**1.9.5**（本柱推进中）。

---

## ★ 当前与排队

> **当前 ★**：**R3 `exhibit-unistd-stdio`** — unistd / stdio 薄封装全称命名。  
> **排队**：R4 printf_format 铺平 → R5 scan_format 铺平 → R6 余下 crt。  
> **刚 TG**：R1 string*；R2 stdlib*/malloc/atexit。

| # | 刀 | 状态 |
|---|-----|------|
| H/X | Hosted 功能柱 | ✅ |
| R1 | string* 展览命名 | ✅ |
| R2 | stdlib* 展览命名 | ✅ |
| R3 | unistd / stdio 薄封装 | 排队 |
| R4 | printf_format 铺平 | 排队 |
| R5 | scan_format 铺平 | 排队 |
| R6 | 余下 crt（time/environ/malloc…） | 排队 |

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-11-04 | 展览级可读性柱 R1～R6；★→R1 |
| 2026-11-04 | P1 自查硬化；CRT 1.9.5 |
