# ISO C / libc 缺口表（selfhost · sh-5）

> **用途**：自举加厚时按表补，不靠猜。活档排刀见 [`libc加厚.md`](libc加厚.md)。  
> **口径**：保留 ISO C99 原名；非 ISO 的 Unix 脸见 Blocks API（`File*` / `Process*` / `Thread*` / `Net*`）。  
> **实现目录**：`Kernel/User/crt/` + `Kernel/User/include/`；产物 `User/Library/Libc/libc.a`。  
> **CRT**：**1.7.0**（PR0～PR9 加厚收口）。

## 已有（课用 + 加厚）

| 域 | 现状 |
| --- | --- |
| 格式化输出 | `printf`/`snprintf`/`vsnprintf`/`puts`/`putchar`；`fprintf`/`vfprintf`/`fputs`/`fputc` |
| 堆 | `malloc` / `calloc` / `realloc` / `free`（brk） |
| 字符串 | `memmove`/`strrchr`/`strncat`/`strtok`/`strerror` 等 |
| 标准库杂项 | `atoi`/`qsort`/`abs`；`strtol`/`strtoul`/`strtod`；`limits.h`；`atexit`/`abort`/`exit` |
| FILE* | 行缓冲写 + `fflush`/`ungetc` |
| 头文件 | `stdbool`/`stdint`/`ctype`/`setjmp`/`time`/`math` |
| 时间 | `clock`/`time`/`gmtime`/`asctime`/`strftime`（垫 `clock_ms`；非完整 TZ） |
| 数学 | `fabs`/`floor`/`ceil`/`sqrt`/`pow`/`fmod`（裁剪） |
| 信号（教学） | `signal` 薄封装 |
| 断言 / errno | `errno`、含 `ERANGE` |

## 缺口（仍后置）

| 优先级 | 缺口 | 说明 |
| --- | --- | --- |
| P2 | `getenv`/`setenv` | 自举脚本需要时再开 |
| P2 | `fscanf` 族 | — |
| P2 | virt `setjmp` | aarch64/riscv 汇编 |
| P2 | 本地化 / 宽字符 | **默认可不做** |
| P3 | 完整 `signal` | — |

## 明确不在本表

- `open`/`fork`/`opendir`/`pthread_*`/`sockaddr` — **不是** ISO C；走 Blocks 名。  
- 对齐 Linux syscall 号、引入 musl — selfhost 红线外。

## 加厚时验收

1. 新符号进 `libc.a`，头在 `User/include`，本表勾选。  
2. `LibcDemo` / Chat / 主范例仍能编。  
3. 不借机恢复假 POSIX 公开脸。

## 与 sh-6

Guest 自举门槛（编译器 + 头 + `libc` → 最小 ELF）依赖本表 P0/P1（已收）后再开（远期）。
