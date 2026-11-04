# Blocks Hosted · Libc

> **没有**单独的 `User/crt/`。用户态 C 一律在本树。

| 目录 | 是什么 |
| --- | --- |
| `stdio/` `stdlib/` `string/` `time/` `math/` `signal/` `errno/` `setjmp/` … | **ISO C** 实现（文件夹名对表头文件） |
| `unistd/` `dirent/` `pthread/` `sched/` `sys/` | Hosted / POSIX 子集（同样按头族） |
| `start/` | **非 ISO**：`_start`、syscall 封装、`call_main`（进 `main` / `environ`） |
| `libc.a` | 上列 `.o` 归档（含 `call_main`）；链接时另加 `start/crt0.o` + `start/syscall.o` |

课用约定：公开符号跟 ISO；实现侧**全称 + 下划线**。活档：`Bingo/App/libc加厚.md`。
