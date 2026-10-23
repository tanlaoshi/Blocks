# Blocks

单仓：Boot · Kernel · Runtime · **Manual** · Documents。

```text
Blocks/
  Manual/      ← 读者手册（按身份：App / Driver / System / Contribute / Course）
  Documents/   ← 日档路线图、过程归档、柱规格
  Boot/  Kernel/  Runtime/  Tools/
  env.sh
```

| 你要… | 去哪 |
| ------ | ---- |
| **读手册 / 上手** | [`Manual/README.md`](Manual/README.md) · [`Manual/START.md`](Manual/START.md) |
| 日档 ★ / 排期 | [`Documents/路线图1023.md`](Documents/路线图1023.md) |
| 从 OVMF 逐段学 | [`原则与学习记录.md`](原则与学习记录.md) |
| 换机 / 工具链 | [`Documents/同步.md`](Documents/同步.md) |
| GitHub | `git@github.com:tanlaoshi/Blocks.git` |

## 怎么开始

```bash
cd ~/Blocks && source ./env.sh
build          # Kernel
run            # QEMU → blocks>
```

细节见 [`Manual/START.md`](Manual/START.md)。
