# 19 · DUO WiFi 单播 ARP 证伪（dwifi-data-1）

> 装 PTK 后先问：**单播数据面是否通**。组播 GTK（KeyID=2）后置。  
> 依赖 Docs/18 眼睛字段；战场：`WifiArp.c` / `WifiDhcp.c` `arpuni=1` / `WifiEapolTx.c`。

---

## 1 · 判据标签（串口原文）

| 标签 | 含义 |
|---|---|
| `unicast-data-ok` | 单播 ARP 有 reply（AP 看见我们 + PTK 数据面至少单向到双向通） |
| `unicast-data-fail` | 发不出，或发出无 reply（空口/串口须一并记下） |

触发：`BLOCKS:probe.cfg` 任意位置写 `arpuni=1`，再跑 `wififw dhcp`。

---

## 2 · 手测 checklist

1. 关联至 `data after keys`；PTK/GTK `cfm=0`。  
2. 写 `arpuni=1` → `wififw dhcp`（或等价入口走到 `WifiDhcpArpUnicastProbe`）。  
3. 串口抄：`data tx: … staid=<ap_idx> mcast=0`（单播，staid=ap）。  
4. NUC 短抓同帧：空口 **KeyID**（期望 PTK=0）/ 是否上空气。  
5. 串口收口行：`unicast-data-ok` **或** `unicast-data-fail` + 原文。  
6. 结论写入路线图 data-1：若 fail → 下一刀优先 **PTK 数据面/控制口**；组播 GTK 后置。若 ok 而组播仍 KeyID=0 → 进 data-2 GTK 选钥。

---

## 3 · 与 ../交接/14-DUO-WiFi数据通路交接.md / 18 的关系

- Docs/18：组播基线仍是 staid=ap、空口 KeyID=0。  
- 本清单只证伪 **单播**；不要用本刀结果去改组播 staid 启发式。
