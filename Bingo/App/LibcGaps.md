# ISO C / libc 缺口表（selfhost · sh-5）

> **用途**：自举加厚时按表补，不靠猜。活档排刀见 [`libc加厚.md`](libc加厚.md)。  
> **口径**：保留 ISO C99 原名；非 ISO 的 Unix 脸见 Blocks API（`File*` / `Process*` / `Thread*` / `Net*`）。  
> **实现目录**：`Kernel/User/crt/` + `Kernel/User/include/`；产物 `User/Library/Libc/libc.a`。

## 已有（课用够用）

| 域 | 现状 |
| --- | --- |
| 格式化输出 | `printf` / `snprintf` / `vsnprintf` / `puts` / `putchar`；`fprintf`/`vfprintf`/`fputs`/`fputc` |
| 堆 | `malloc` / `calloc` / `realloc` / `free`（brk） |
| 字符串 | 上列 + `memmove` / `strrchr` / `strncat` / `strtok` / `strerror` |
| 标准库杂项 | `atoi` / `qsort` / `exit` / `abs`；`strtol`/`strtoul`/`strtod`；`limits.h` |
| FILE* | 无缓冲 `fopen` / `fread` / `fwrite` / `fclose` / `fseek` / `ftell`（底层走 `File*`） |
| 信号（教学） | `signal` 薄封装（非完整 POSIX 信号） |
| 断言 / errno | `errno`、常见 `E*` |
| 头文件 | `stdbool.h` / `stdint.h`；`ctype.h` ASCII 分类+大小写；`sys/types.h` 经 `stdint` |

## 缺口（自举前优先）

按「Guest 内编最小 C 工具链」相关度排序；未勾的先别当承诺。

| 优先级 | 缺口 | 说明 |
| --- | --- | --- |
| P0 | 更完整的 `stdio` | ✅ fmt 族；缓冲/`ungetc`→PR5；`fscanf`/宽字符后置 |
| P0 | `stdlib` 解析 | ✅ `strtol`/`strtoul`/`strtod`；`getenv`/`setenv` 后置 |
| P1 | `string.h` 补全 | ✅ libc-string（`memmove` 原有） |
| P1 | `ctype.h` / `stdbool.h` / `stdint.h` | ✅ libc-headers |
| P1 | `setjmp` / `longjmp` | 编译器运行时 / 错误恢复常要 |
| P2 | `time.h` | `time`/`clock`/`strftime`（可先薄封装 `clock_ms`） |
| P2 | `math.h` | 软浮点或裁剪子集（视工具链） |
| P2 | 本地化 / 宽字符 | `locale`/`wchar`：**默认可不做** |
| P3 | 完整 `signal` / `atexit` / `abort` | 按自举链接器需求再加 |

## 明确不在本表

- `open`/`fork`/`opendir`/`pthread_*`/`sockaddr` — **不是** ISO C；走 Blocks 名（见 [README §0.1](README.md)）。
- 对齐 Linux syscall 号、引入 musl — selfhost 红线外。

## 加厚时验收

1. 新符号进 `libc.a`，头在 `User/include`，Manual 本表勾选。  
2. `LibcDemo` / Chat / 主范例仍能编。  
3. 不借机恢复假 POSIX 公开脸。

## 与 sh-6

Guest 自举门槛（编译器 + 头 + `libc` → 最小 ELF）依赖本表 P0/P1 收口后再开（远期）。
