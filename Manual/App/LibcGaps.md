# ISO C / libBlocks 缺口表（selfhost · sh-5）

> **用途**：自举加厚时按表补，不靠猜。  
> **口径**：保留 ISO C99 原名；非 ISO 的 Unix 脸见 Blocks API（`File*` / `Process*` / `Thread*` / `Net*`）。  
> **实现目录**：`Kernel/User/crt/` + `Kernel/User/include/`。

## 已有（课用够用）

| 域 | 现状 |
| --- | --- |
| 格式化输出 | `printf` / `snprintf` / `vsnprintf` / `puts` / `putchar` |
| 堆 | `malloc` / `calloc` / `realloc` / `free`（brk） |
| 字符串 | `strlen` / `memcpy` / `memcmp` / `strcmp` / `strncmp` / `strcpy` / `strncpy` / `strstr` / `strchr` / `strdup` 等子集 |
| 标准库杂项 | `atoi` / `qsort` / `exit` / `abs` 等子集 |
| FILE* | 无缓冲 `fopen` / `fread` / `fwrite` / `fclose` / `fseek` / `ftell`（底层走 `File*`） |
| 信号（教学） | `signal` 薄封装（非完整 POSIX 信号） |
| 断言 / errno | `errno`、常见 `E*` |

## 缺口（自举前优先）

按「Guest 内编最小 C 工具链」相关度排序；未勾的先别当承诺。

| 优先级 | 缺口 | 说明 |
| --- | --- | --- |
| P0 | 更完整的 `stdio` | 缓冲、`fprintf`/`fscanf` 族、`ungetc`、宽字符流 |
| P0 | `stdlib` 解析 | `strtol`/`strtoul`/`strtod`、`getenv`/`setenv`（若自举脚本需要） |
| P1 | `string.h` 补全 | `memmove`/`strerror`/`strtok`/`strrchr`/`strncat` 等 |
| P1 | `ctype.h` / `stdbool.h` / `stdint.h` | 若头不齐或实现为 stub，先钉齐 |
| P1 | `setjmp` / `longjmp` | 编译器运行时 / 错误恢复常要 |
| P2 | `time.h` | `time`/`clock`/`strftime`（可先薄封装 `clock_ms`） |
| P2 | `math.h` | 软浮点或裁剪子集（视工具链） |
| P2 | 本地化 / 宽字符 | `locale`/`wchar`：**默认可不做** |
| P3 | 完整 `signal` / `atexit` / `abort` | 按自举链接器需求再加 |

## 明确不在本表

- `open`/`fork`/`opendir`/`pthread_*`/`sockaddr` — **不是** ISO C；走 Blocks 名（见 [README §0.1](README.md)）。
- 对齐 Linux syscall 号、引入 musl — selfhost 红线外。

## 加厚时验收

1. 新符号进 `libBlocks.a`，头在 `User/include`，Manual 本表勾选。  
2. `LibcDemo` / Chat / 主范例仍能编。  
3. 不借机恢复假 POSIX 公开脸。

## 与 sh-6

Guest 自举门槛（编译器 + 头 + `libBlocks` → 最小 ELF）依赖本表 P0/P1 收口后再开；见日档 `路线图1024.md` · sh-6（远期）。
