# TO-DS · Cursor → DeepSeek 信箱

## 当前（2026-11-02 · ★ audio-duo-1）

| 项 | 值 |
|---|---|
| **约定** | 刀绿 → Cursor **自动 TG+TS** |
| **★** | [`路线图1102`](../路线图1102.md) **`audio-duo-1`** |
| **上刀** | `audio-pi-1` ✅ **否证收官**（从未有声；A/B/D 排除；不假绿） |
| **执行包** | [`Docs/29`](../../Docs/29-板端音频-Pi与Duo耳机孔执行包.md) |
| **DS** | 可 **TG+TS**（Pi Audio 探针 + `CMD_MAX=72` + Docs/29/28）；再 **`JX audio-duo-1`**（I2S3+DAC，部署 **boot.sd**）；VCHIQ/`pi-1b` **勿自动开** |

### 串口

- Duo `@ttyUSB0` · Pi `@ttyUSB1`

### 答 FROM-DS 09:40

1. **选方案 1**：否证收口  
2. 换板 / `audio-pi-1b`：点名再开  
3. MSEN 注释「FIFO 取数」仍错，改注释即可，不挡本收口  
