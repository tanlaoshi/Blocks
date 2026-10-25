# Board 族：Raspberry（Arm64）

树莓派真机板包落在本目录下；**一板一子目录**。

| 子目录 | `BOARD=` | 状态 |
| --- | --- | --- |
| [`Pi3BPlus/`](Pi3BPlus/) | `Raspberry/Pi3BPlus` | 今日工作目录（路线图1023 · rpi-desk） |

约定总览：[`HAL/Common/Board/README.md`](../../../Common/Board/README.md)。  
QEMU 课堂靶仍是同级 [`../virt/`](../virt/)（`BOARD=virt`）。

```bash
source ~/Blocks/env.sh
build arm64 BOARD=Raspberry/Pi3BPlus LWIP=0
boards arm64    # 应列出 virt 与 Raspberry/Pi3BPlus
```
