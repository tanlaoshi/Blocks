# Manual / Driver — 驱动开发者

> 目标：按模板登记 / Probe / Bind；匹配走规范；外设先查规格。

| 文档 | 内容 |
| ---- | ---- |
| [`Guide.md`](Guide.md) | 五步写驱动、目录规则、BOOT 序 |
| [`Match.md`](Match.md) | 匹配三模式（表 / 自扫 / …） |

**代码入口**：`Kernel/HAL/X64/Drivers/_template/` · `Kernel/Include/Driver/`。  
**铁律**：键鼠优先、禁止半截方案（见 [`../Contribute/Coding.md`](../Contribute/Coding.md) §鼠标优先）；USB/xHCI 等以规格为准。  
**上机**：[`../START.md`](../START.md)。
