# ISO C / libc 缺口表

> **口径**：**对齐 C99 freestanding**（非 Linux hosted）。应用侧 `printf`/`fopen` 等是 **Blocks 自用加厚**，不宣称 hosted 合规。  
> **产物**：`User/Library/Libc/libc.a`；活档 [`libc加厚.md`](libc加厚.md)。  
> **CRT**：**1.8.0**。

## Freestanding（C99 强制七头）

| 头 | 状态 |
| --- | --- |
| `float.h` | ✅ 1.8.0 |
| `iso646.h` | ✅ 1.8.0 |
| `limits.h` | ✅ |
| `stdarg.h` | ✅ |
| `stdbool.h` | ✅ |
| `stddef.h` | ✅ |
| `stdint.h` | ✅ |

→ **freestanding 头面已齐**（语义未跑合格套件）。

## 自用加厚 · 已有（摘）

`printf` 子集、`malloc` 族、`string` 常用、`fopen`/`fread`/`fwrite`、行缓冲、`strtol`/`strtod`、薄 `math`/`time`、`setjmp`(x64)、`atexit`…

## 函数功能差多少（对照实现，非 hosted 全表）

### `printf` / `sprintf` / `snprintf`（`printf.c`）

| 已有 | 缺（常用） |
| --- | --- |
| `%s` `%c` `%d` `%u` `%x`/`%X` `%%` | **`%a`**（可不做） |
| **`%f` `%e` `%g`**（教学；支持 `.prec`） | 完整舍入/长 double/`L` 修饰 |
| `%0`+宽度（主要对 `%x`） | **`%l` `%ll` `%z` `%p`** 长度修饰 |
| | **`%i` `%o`**；对齐（`-` `+` ` ` `#`）大半无 |
| | **`%n`**（可不做） |

→ 浮点格式 **PR11 已补教学子集**；下一刀长度修饰。

### `stdio` 其它

| 已有 | 缺 |
| --- | --- |
| `fopen`/`fclose`/`fread`/`fwrite`/`fseek`/`ftell` | **`stdin`/`stdout`/`stderr`** |
| `fprintf`/`fputs`/`fputc`/`fflush`/`ungetc` | **`fgets`/`gets`/`getchar`/`scanf`/`fscanf`/`sscanf`** |
| | `setvbuf`/`setbuf`；`remove`/`rename`/`tmpfile`（需系统 unlink 等） |

### `stdlib` / `string` / `math` / `time`（自用）

| 域 | 缺（若还要加厚） |
| --- | --- |
| stdlib | `bsearch`/`rand`/`srand`/`div`；`getenv`（需 environ） |
| string | `strpbrk`/`strspn`/`strcspn`/`strcoll`… |
| math | `sin`/`cos`/`log`/`exp`/…（现仅 6 个） |
| time | 真墙上钟；`mktime`/`localtime` |
| setjmp | virt aarch64/riscv |

## 明确不做

- C99 **hosted** 全表 / Linux 对标  
- `locale` / `wchar` / `complex` / 完整 `fenv`  
- musl；假 POSIX 公开脸  

## 与系统支撑（仅当要做对应函数时）

| 能力 | 挡住谁 |
| --- | --- |
| 标准三流 FILE* | `stdout`/`scanf` 默认流 |
| environ | `getenv` |
| 墙上钟 | 真 `time()` |
| unlink/rename | `remove`/`rename`/`tmpfile` |
