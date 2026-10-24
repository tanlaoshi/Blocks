# Manual / App — 应用层开发者

> 目标：静态链接 ELF → 进 Guest `exec` →（可选）商店目录包。  
> 活 ★：[路线图1024 · selfhost](../../Documents/路线图1024.md#selfhost)。

| 文档 | 内容 |
| ---- | ---- |
| [`Guide.md`](Guide.md) | 上手：CRT / UI / Net / 打包 |
| [`ABI.md`](ABI.md) | 开课冻结：版本与承诺 / 不承诺 |
| [`API.md`](API.md) | 一页 API 表 |
| [`LibcGaps.md`](LibcGaps.md) | ISO C 加厚缺口（selfhost · sh-5） |
| [`UI/`](UI/) | 布局文件、多页面、拖控件设计器 |

**代码入口**：`Kernel/Tools/Sdk/Examples/` · `Kernel/User/Apps/` · 头文件 `Kernel/User/include/`。  
**上机**：[`../START.md`](../START.md)。  
编码规矩（贡献向）：[`../Contribute/Coding.md`](../Contribute/Coding.md)。

---

## 0.1 身份（selfhost · 2026-10-24 落 main）

这是**全新系统**。北辰是 **Blocks 自举**（将来在 Blocks 上编 Blocks），不是「Linux 兼容层」。

| 要 | 不要 |
| --- | --- |
| **C 标准库**（语言运行时：printf / malloc / string / 文件流…）— 必须，按需加厚到可自举 | **模仿 Linux / 假 POSIX**（为眼熟而设的双轨、别名、`sockaddr` 课设） |
| **Blocks 自己的规则**：API 名、调用约定、文档口径 | 函数别名「再挂一个 Linux 名」；新代码再开双轨 |
| syscall **号段本就自有**（`SyscallABI.h`，非 Linux 0/1/2…） | 为兼容去对齐 Linux 号表 |

**当断就断**：归大柱 **[selfhost](../../Documents/路线图1024.md#selfhost)**。彻底还清 = 对外只剩 **ISO C + Blocks API**；序：**叙事 → 砍选修轨/别名 → 本位改名 → 加厚 C 库 → 自举**。

一句话：**C 库是母语；Linux 脸是债。**

**语言**：锚定 **ISO C99**（`-std=c99`）；不要 C11、不要 GNU 方言。硬件布局用的 `packed`/`aligned`、以及 freestanding `va_list`，只许薄编译器垫片，不在业务代码里散落 `__attribute__`/`__builtin_*`。

### SDK 三层（记这个就够）

| 层 | 链什么 | 干什么 | 头 |
| --- | --- | --- | --- |
| **底** | `crt0` + `syscall` + **`libBlocks.a`** | 进进程、printf/malloc/文件… | ISO C 头 + `<Blocks/…>` |
| **窗** | + **`libUi.a`** + **`libGraphics.a`** | 开窗、控件、画 | `<Ui.h>` `<Graphics.h>` |
| **网** | + **`libNet.a`** | TCP（**只教** `NetConnect`…） | `<Net.h>` 或 `<ToyNet.h>`（同 API） |

**明确不要**：新文档/新 API 再堆 Linux 眼熟、POSIX 双轨、别名。
