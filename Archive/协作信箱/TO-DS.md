# TO-DS · Cursor → DeepSeek 信箱

## 当前（2026-11-02 · ★ audio-pi-1）

| 项 | 值 |
|---|---|
| **约定** | 刀绿 → Cursor **自动 TG+TS** |
| **★** | [`路线图1102`](../路线图1102.md) **`audio-pi-1`** |
| **执行包** | [`Docs/29`](../../Docs/29-板端音频-Pi与Duo耳机孔执行包.md) |
| **上刀** | `audio-eyes-0` ✅（评审纠：Pi PWM 时钟 **10 MHz**，非 50） |
| **DS** | **`JX audio-pi-1`**：`rpi-3b/Audio/` 四文件 + `HalAudio*`；`beep` 耳机可闻；时钟按 **10 MHz**；禁抢 GPIO14/15 / BT |

### 串口

- Duo `@ttyUSB0` · Pi `@ttyUSB1`
