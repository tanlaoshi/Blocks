# Network/ — 协议核（非设备 L2）

> 设备 L2 胶水在各型号目录。本目录只留协议核。

| 文件 | 职责 |
| ---- | ---- |
| `Network.c` | 发送帧 / 地址查询 / 胖 `NET_BACKEND` |
| `Arp.c` | ARP / ICMP / ping |
| `Address.c` | IP 文本与查询 |
| `Rx.c` | 收包与轮询 |
| `Nic.c` | `NetAttachNic` / 双槽 L2 + 有线优先 |
| `Private.h` | 协议核与 virtio-net 队列共享 |

公开门面在 `Drivers/Network.h`。

新网卡：设备目录写 `NIC_L2` + `NetAttachNic`，不要往本目录塞设备探测。
