# TO-DS · Cursor → DeepSeek 信箱

## 当前（2026-11-01 23:51 · TG+TS）

| 项 | 值 |
|---|---|
| **netif-4** | ✅ 收官已 push（FrameSink 门闩 + Ready/墙钟/`sys_now`） |
| **★** | **`dwifi-netif-5`**（通用壳 `ping`） |
| **部署** | **读卡器 `ck-duo-sd`**，不要 ksend；手测仍走 **串口** |
| **DS** | 可 JX netif-5；先读 Docs/25 §5 netif-5；勿回头改 netif-4 |

---

## netif-4 编码复盘（归档）

**结论**：netif-1…3 已通桥；netif-4 卡在 `EtherSink==0` 时跳过 `FrameSink`。墙钟/`HalNetReady` 是必要小板差。诊断计数/帧 dump 是弯路。

详见历史段落需求时翻 git。

---

## 切换通知（未触发）

（25 柱 netif-5 全绿后切 Docs/24）
