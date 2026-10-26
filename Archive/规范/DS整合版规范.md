# 代码规范

**版本**：1.1  
**状态**：生效  
**适用**：内核、HAL、Services、User 自有代码、构建脚本  
**例外**：POSIX / CRT、第三方代码、盘上 8.3 名、脚本文件名保持原样

## 规范性用语

| 用语 | 含义 |
| ---- | ---- |
| **必须 / 不得** | 强制条款；违反需在 review 中说明并补例外 |
| **应当** | 默认遵守；确有理由可不遵守，但需在变更说明中写清 |
| **可以** | 可选做法 |

---

## 1. 总则

**T-1 目标**  
有一点 C 基础的人，靠名字能顺着读；靠文件头知道这文件干什么；靠分层知道该改哪一层。注释只写「为什么」，不复述「做了什么」。

**T-2 新代码强制**  
新写、新改的代码必须遵守本文。旧代码不要求一次翻库。

**T-3 旧代码增量清理**  
点到哪个文件，顺手把眼前的命名、注释、魔法数收干净；行为不变。不跨文件大搬家。

**T-4 例外保持原样**  
POSIX / CRT（`fopen`、`malloc` 等）、第三方（lwIP 等）、脚本文件名、盘上 8.3 名，不按本文改名。

**T-5 规则冲突**  
本文与旧细则冲突时，以本文为准；本文未覆盖的，按附录 B 备查。

---

## 2. 命名

### N-1 标识符风格

- 函数、变量、参数、文件名、目录名：**PascalCase 全词**。
- 类型、枚举、宏：**ALL_CAPS**，下划线分隔。
- 全局变量：`g` + PascalCase；文件内静态变量宜 `s` + PascalCase。
- 不得使用 snake_case、camelCase、拼音、匈牙利前缀。

| 要 | 不要 |
| -- | ---- |
| `SerialInitialize` | `serial_init`、`serialInitialize`、`Serial_Initialize` |
| `gCommandCount` | `gCmdCount`、`g_command_count` |
| `TOY_DRIVER` | 用 `ToyDriver` 作 typedef 名 |

**允许的短名**：`i` / `j` / `k`（简单循环）、`x` / `y` / `z`（坐标）、`fd`、`id`、`pid`。

### N-2 名词在前，动词在后

层前缀在最前；层前缀之后先写对象/子系统，再写动作。

| 要 | 不要 |
| -- | ---- |
| `ConsoleInitialize` | `InitializeConsole` |
| `HalSerialWrite` | `HalWriteSerial`、`HalInitializeSerial` |
| `PhysicalMemoryAllocatePages` | `AllocatePhysicalMemoryPages` |

**例外**：模块操作表里的字段仍用短动作名，见 N-7。

### N-3 目录与源文件命名

- 目录、`.c` / `.h` 文件：PascalCase。
- 主文件与目录同名：`Serial/Serial.c`、`XHCI/Xhci.c`。
- 脚本：kebab-case，仅限 `*.sh` 与仓库根构建入口。
- CRT / 链接脚本 / POSIX 头保持惯例：`crt0.S`、`user.ld`、`stdio.h`。

### N-4 辅助文件不重复模块名

文件过大拆成「1 个核心文件 + 多个辅助文件」，放在同一目录。目录已经表达模块，辅助文件不再加模块前缀。

| 要 | 不要 |
| -- | ---- |
| `XHCI/Xhci.c` | 把主逻辑改名，使目录里没有主文件 |
| `XHCI/UsbHostController.c` | `XHCI/XhciUsbHostController.c` |

拆分形态见 F-3。

### N-5 函数名不重复文件名

文件已经说明是哪一块，函数名只写自己的动作。

| 要 | 不要 |
| -- | ---- |
| `InterruptQueue` | `UsbHostControllerInterruptQueue` |

### N-6 头文件命名

- 主头与目录同名：`Serial/Serial.h`。
- 辅助头不重复目录名，统一用 `Private.h` 或 `Internal.h`，同一模块内择一。
- 包含时带目录：`Ehci/Private.h`，不依赖 `-I` 顺序区分同名头。

| 要 | 不要 |
| -- | ---- |
| `Ehci/Private.h` | `Ehci/EhciPrivate.h` |
| `Iwl/Private.h` | `Iwl/IwlPrivate.h` |

### N-7 短函数名进操作表

内核一次链接，全局只有一张符号表。`Write`、`Initialize` 这类短名不得各模块各有一份全局函数。

一个模块对外一张操作表：

- 表字段用短动作名；
- 真正干活的函数写 `static`，短名只在本文件；
- 别的目录通过操作表调用，不在头文件里再声明一排全局短名；
- 模块初始化时把表填上；
- 只在本文件用的函数直接 `static`，不必进表；
- 不靠给全局函数加长前缀来躲撞名，收进操作表。

### N-8 缩写白名单

只保留公认缩写，见附录 A。白名单外的缩写必须写全，例如：

`Init`→`Initialize`、`Cfg`→`Configure`、`Msg`→`Message`、`Buf`→`Buffer`、`Len`→`Length`、`Ptr`→`Pointer`、`Idx`→`Index`、`Cnt`→`Count`、`Ctx`→`Context`、`Param`→`Parameter`、`Attr`→`Attribute`、`Desc`→`Descriptor`、`Reg`→`Register`、`Addr`→`Address`、`Fn`→`Function`、`Drv`→`Driver`、`Sched`→`Scheduler`、`Fs`→`FileSystem`、`Mem`→`Memory`、`Win`→`Window`、`Wm`→`WindowManager`、`Cmd`→`Command`、`Runq`→`RunQueue`。

### N-9 日志文案

日志单词用 PascalCase；缩写用 ALL_CAPS；前缀用 `Boot:` / `[Mod]`，不用 `boot:` / `[mod]`。

| 要 | 不要 |
| -- | ---- |
| `Boot: GetAndSetVideo` | `boot: GetAndSetVideo` |
| `COM1 Serial OK` | `com1 serial ok` |
| `[Mod] VirtualMemory` | `[mod] virtual-memory` |

---

## 3. 分层与依赖

### L-1 依赖方向

**上层依赖下层**，只通过门面调用：

`User` 依赖 `Services`，`Services` 依赖 `Core`，依此类推。下层不得反向依赖上层。

### L-2 启动顺序

`Boot → HAL → Core → Services → User`

启动顺序与 L-1 的依赖方向相反，二者不混用：L-1 管「谁能调谁」，L-2 管「谁先起来」。

### L-3 禁止跨层包含

- Common / Include / User 不得直接 `#include` 某架构的 `HAL/.../Drivers/...`。
- User 不得包含内核内部头。
- Library 不得依赖 Services 的 `Console.h`。
- 共享层不得堆 `BOARD_` / `SOC_` ifdef。

### L-4 板级差异走能力旗标

板级差异用能力旗标或 HAL 门面表达，不在共享层堆板级条件编译。

### L-5 主函数是顺序表，不是策略表

入口函数只写「先做什么、再做什么」。分支和失败处理沉到被调函数里。

| 主函数该有 | 主函数不该有 |
| ---- | ---- |
| `InitializeXxx()` 调用 | `if (HalHasFrameBuffer())` |
| `SchedulerCreate()` | `if (LWIP)` |
| `SchedulerStart()` | `#ifdef TOY_X64` |
| — | `if (Init() != 0) Halt()` |

平台分发集中在 `KernelMain` 一处；设备缺失、初始化失败写在对应 `Initialize*` 内部。

---

## 4. 文件与函数组织

### F-1 模块标准形状

主文件只做三件事：

1. 定义对外 API（薄门面，每个 ≤15 行）；
2. 定义主函数（≤30 行，顺序调辅助文件）；
3. 调辅助文件里的函数。

辅助文件按能力切分，只搬家、不改行为。

### F-2 规模上限

| 对象 | 上限 |
| ---- | ---- |
| 主文件对外 API 每个函数 | 15 行 |
| 主函数（顺序表） | 30 行 |
| 任意函数 | 60 行 |
| 任一 `.c` | 300 行 |
| `.md`、`Archive/**`、讲义、路线图 | 无上限 |

新代码超标必须先拆再写。存量 `.c` 超标按模块分刀，不一次清全树。点阵、`ThirdParty/`、构建脚本不按 300 行拆。

### F-3 拆分形态

固定为「1 个核心文件 + 多个辅助文件」，辅助文件命名规则见 N-4。

| 核心文件 | 辅助文件示例 |
| ---- | ---- |
| `Console.c` | `ConsoleWrite.c` / `ConsoleScroll.c` / `ConsoleCmd.c` |
| `GuiWm.c` | `GuiHit.c` / `GuiOpen.c` / `GuiPointer.c` |

---

## 5. 注释

### C-1 文件头必须说清三件事

每个被改到的 `.c` 和公开 `.h` 顶部使用中文人话文件头，至少包含：

1. **人话**：这文件在用户眼里是什么，一句话场景；
2. **从哪读**：建议的阅读顺序，2～4 个函数名；
3. **别改**：和别人的契约（ABI、顺序、锁），可选。

可以加「想照着做」指向已有短文；没有就写「暂无」。

```c
/*
 * 人话：这里是「任务管理器」窗口程序。开机后在开始菜单里点它，
 *       或在 Shell 里 exec TASKMGR.ELF。它列出内核里正在跑的任务。
 *
 * 从哪读：main（文件末尾）→ Refresh 拉一张快照 → Paint 画当前这一行。
 *
 * 别改：toy_task_snap / TOY_TASK_SNAP 的字段含义（与 Shell 命令 ps 同一份）。
 *       按钮编号 0/1/2 与 ToyUiAddButton 的添加顺序绑死。
 *
 * 想照着做：Archive/开发/如何写一个任务管理器App.md
 */