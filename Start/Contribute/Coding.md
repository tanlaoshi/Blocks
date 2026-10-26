# 代码规范

> **Start 正本**：本文件在 `Start/Contribute/` 下维护；`Archive/手册/` 同期副本仅过渡期保留。


> **写新代码只认本文。** 短、可改；细则以后一点一点往上加。  
> 旧的长篇细则暂放 [细则备查/](../../Archive/规范/细则备查/)，不日常翻。  
> 教学 review 总原则见 [`../../Archive/原则与学习记录.md`](../../Archive/原则与学习记录.md)。

---

## 一句话

有一点 C 基础的人，靠**名字**能顺着读；靠**文件头**知道这文件干什么；靠**分层**知道该改哪一层。注释只写「为什么」，不复述「做了什么」。

---

## 核心原则

### 1. 名字即文档

| 要 | 不要 |
| -- | ---- |
| PascalCase 全词：`SerialInitialize` | `serial_init` / `InitSerial` / `Cfg` |
| 名词在前、动词在后：`HalSerialWrite` | `HalWriteSerial` |
| 层前缀在最前：`Hal*` / `Toy*` / `Board*` | 动词抢在层名前面 |
| 类型 / 宏 `ALL_CAPS`；全局 `gXxx`；文件内静态宜 `sXxx` | 公开 API 用拼音或缩写谜语 |

**例外（保持原样）**：**ISO C** 标准库名（`fopen`、`malloc`、`printf`…）、第三方（lwIP 等）、脚本文件名、盘上 `8.3` 名。  
**非 ISO C 的 Unix/假 POSIX 脸**（`open`/`fork`/`sockaddr`/`pthread_*`…）**不在例外里**——走 Blocks 本位名（见 [App §0.1](../App/README.md) / selfhost）。

### 2. 分层不焊死、不跨层

依赖方向：`User → Services → Core → Library → HAL → Boot`。  
上层只通过门面调下层；Common / Include / User **不**直接 `#include` 某架构的 `HAL/.../Drivers/...`。板级差异走能力旗标，不在共享层堆 `BOARD_` ifdef。

### 3. 主路径像说明书

- 一个 `.c` 大约 **≤300 行**；太大就「1 个核心文件 + 多个辅助文件」，只搬家、不改行为。
- 入口函数是**顺序表**（先做什么、再做什么），分支和失败处理沉到被调函数里。
- 单函数别写成一整篇故事；能拆就拆。

### 4. 注释写「为什么」

文件头用中文人话，至少说清：

1. **这文件是干什么的**（用户眼里的场景）  
2. **从哪几个函数读起**  
3. **和谁有契约、别乱改什么**（可选）

禁止：每行复述代码；把 PR 号当唯一说明；把标识符改成拼音。

### 5. 数字和错误要露脸

- 寄存器偏移、超时、尺寸：起名字（`#define` / `const`），别扔裸 `0x1234`。  
- 可能失败的调用：处理错误，或显式 `(void)` 表示有意忽略。  
- 改用户可见接口：先改头文件 → [开课ABI冻结](../App/ABI.md) → 速查卡 / API速查 / 应用指南。

### 6. 新代码守规矩，旧代码路过再改

不要求一次翻全库。点到哪个文件，顺手把眼前的命名、注释、魔法数收干净；行为不变。

### 7. Kernel 方言：ISO C99

自有树（`Kernel/{Core,Services,HAL自研,BringUp,Library,Include,User,Scripts}`）锁 **`-std=c99 -pedantic`**。业务不写 `_Static_assert` / 手写 `__attribute__` / `__builtin_*` / `__sync_*`；垫片只在 `Include/Abi/Compiler.h`、`Include/Abi/Atomic.h` 与 `User/include/stdarg.h`。

例外：`Boot/`（跟 EDK2）、`ThirdParty/`、lwIP（`-std=gnu11 -Wno-pedantic`）、FreeType（`CFLAGS_FT`）。

门禁（在 `Kernel/` 下，应无业务命中）：

```bash
rg '__attribute__' . --glob '!ThirdParty/**' --glob '!Build/**' -g '*.{c,h,S}' | grep -v Include/Abi/Compiler.h
rg '__builtin_'    . --glob '!ThirdParty/**' --glob '!Build/**' -g '*.{c,h,S}' | grep -v Include/Abi/Compiler.h | grep -v User/include/stdarg.h
rg '__sync_'       . --glob '!ThirdParty/**' --glob '!Build/**' -g '*.{c,h,S}' | grep -v Include/Abi/Atomic.h
```

### 8. 鼠标优先（禁止半截方案）

与 [`.cursor/rules/standards-first.mdc`](../../.cursor/rules/standards-first.mdc) 同文：键鼠最优先于 MSC/网/合成。

| 要 | 不要（图省事） |
| -- | ---- |
| 光标一出现就流畅；慢活做完再 Reveal | 早画光标，再靠 freeze/重试碰运气保鼠 |
| 未 Reveal：Gui 不泵鼠，Input 少 Poll | 只「不跟坐标」，仍 `HalInputPoll`/倒队列抢环 |
| Present 短关中断只护 Under；脏矩形要小 | 整屏 `PresentFlush`、2Hz `HubGetPortStatus`、Present 持 `IrqSave` |

验收：晃鼠标会不会拖死 Reveal/网托？露光标后空拖还有没有规律顿（对表 WorkerTick/HotPoll/CMOS）？

---

## 验收时问自己

1. 没看注释，名字能不能猜出职责？  
2. 文件头能不能让外人 1 分钟内知道从哪读？  
3. 改完是否还在正确的层？有没有抄近路跨层？  
4. QEMU 主路径还能编过、还能启动吗？  
5. 若动了键鼠/xHCI/光标：有没有违反「鼠标优先 · 禁止半截方案」？

---

## 以后怎么加长

需要细则时，在本文加一小节即可；从 [细则备查/](../../Archive/规范/细则备查/) 里抄有用的段落上来，不要把备查整册重新当成入口。
