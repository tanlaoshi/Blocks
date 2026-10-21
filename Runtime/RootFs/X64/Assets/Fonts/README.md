# Assets/Fonts

| 文件 | 用途 | 许可 |
|------|------|------|
| `CJK.TTF` | UI 中英同源（AR PL UMing CN 子集，glyf） | Arphic Public License |
| `*.FNT` | TOYF 点阵（可选） | 见各包 |
| 内建 Terminus | ASCII 软退 | GPL-2.0 |

重生 UI 子集（宿主机需 `fonttools` + `fonts-arphic-uming`）：

```bash
python3 Tools/Scripts/gen-cjk-ttf-subset.py
cp Tools/Fonts/CJK.TTF Runtime/RootFs/X64/Assets/Fonts/
cp Tools/Fonts/CJK.TTF Runtime/Assets/Fonts/
```
