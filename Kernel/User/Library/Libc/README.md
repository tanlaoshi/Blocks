# Library/Libc · ISO C 实现

**只放 ISO C。** 头在 `User/Include/`；此处为有 `.c`/`.S` 的实现。

| 目录 | 头 |
| --- | --- |
| `errno/` | `<errno.h>` |
| `math/` | `<math.h>` |
| `setjmp/` | `<setjmp.h>` |
| `signal/` | `<signal.h>` |
| `stdio/` | `<stdio.h>` |
| `stdlib/` | `<stdlib.h>` |
| `string/` | `<string.h>` |
| `time/` | `<time.h>` |

Freestanding 七头仅在 `include/`，不建空目录。  
非 ISO → [`../Blocks/`](../Blocks/)。  
缺口：[`Archive/项目/LibcGaps.md`](../../../../Archive/项目/LibcGaps.md)。
