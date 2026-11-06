#!/usr/bin/env bash
# 安装 HostChat 到应用菜单 + 桌面；Dock 对齐 com.blocks.hostchat
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/Build/meson/hostchat-gui"
DESKTOP_NAME="com.blocks.hostchat.desktop"
ICON_NAME="com.blocks.hostchat"
ICON_DIR="$HOME/.local/share/icons/hicolor"

if [[ ! -x "$BIN" ]]; then
  echo "missing binary: $BIN — building…" >&2
  if ! command -v meson >/dev/null 2>&1; then
    echo "error: need meson" >&2
    exit 1
  fi
  meson setup "$ROOT/Build/meson" "$ROOT" >/dev/null
  meson compile -C "$ROOT/Build/meson" hostchat-gui
fi

# 简易气泡图标（无外链品牌图）
python3 - <<'PY' || true
from pathlib import Path
try:
    from PIL import Image, ImageDraw
except ImportError:
    raise SystemExit(0)
home = Path.home()
for size in (16, 24, 32, 48, 64, 128, 256):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    m = max(1, size // 16)
    # 绿底圆角块 + 白气泡
    draw.rounded_rectangle((m, m, size - m - 1, size - m - 1),
                           radius=size // 5, fill=(7, 193, 96, 255))
    bx0, by0 = size * 0.18, size * 0.22
    bx1, by1 = size * 0.82, size * 0.62
    draw.rounded_rectangle((bx0, by0, bx1, by1), radius=size // 8,
                           fill=(255, 255, 255, 255))
    # 小尖角
    tip = [
        (size * 0.28, by1 - 1),
        (size * 0.28, size * 0.78),
        (size * 0.48, by1 - 1),
    ]
    draw.polygon(tip, fill=(255, 255, 255, 255))
    dest = home / f".local/share/icons/hicolor/{size}x{size}/apps"
    dest.mkdir(parents=True, exist_ok=True)
    img.save(dest / "com.blocks.hostchat.png")
    img.save(dest / "hostchat.png")
print("icons ok")
PY

ICON_FILE="$ICON_DIR/128x128/apps/${ICON_NAME}.png"
if [[ ! -f "$ICON_FILE" ]]; then
  # 无 Pillow 时退回主题名
  ICON_FILE="preferences-desktop-online-accounts"
fi

mkdir -p "$HOME/.local/share/applications"
rm -f "$HOME/.local/share/applications/hostchat.desktop"

cat >"$HOME/.local/share/applications/${DESKTOP_NAME}" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=HostChat
Name[zh_CN]=HostChat 聊天
Comment=Blocks Guest Chat host (relay + GUI)
Comment[zh_CN]=Blocks Guest 聊天宿主（中继 + GUI）
Exec=${BIN}
Path=${ROOT}
Icon=${ICON_FILE}
Terminal=false
Categories=Network;InstantMessaging;Utility;
StartupNotify=true
StartupWMClass=com.blocks.hostchat
X-GNOME-UsesNotifications=false
EOF
chmod 644 "$HOME/.local/share/applications/${DESKTOP_NAME}"

DESKTOP_DIR="${XDG_DESKTOP_DIR:-}"
if [[ -z "$DESKTOP_DIR" ]] && command -v xdg-user-dir >/dev/null 2>&1; then
  DESKTOP_DIR="$(xdg-user-dir DESKTOP)"
fi
if [[ -z "$DESKTOP_DIR" || ! -d "$DESKTOP_DIR" ]]; then
  if [[ -d "$HOME/桌面" ]]; then
    DESKTOP_DIR="$HOME/桌面"
  elif [[ -d "$HOME/Desktop" ]]; then
    DESKTOP_DIR="$HOME/Desktop"
  fi
fi
if [[ -n "${DESKTOP_DIR:-}" && -d "$DESKTOP_DIR" ]]; then
  rm -f "$DESKTOP_DIR/hostchat.desktop"
  cp -f "$HOME/.local/share/applications/${DESKTOP_NAME}" "$DESKTOP_DIR/${DESKTOP_NAME}"
  chmod +x "$DESKTOP_DIR/${DESKTOP_NAME}"
  if command -v gio >/dev/null 2>&1; then
    gio set "$DESKTOP_DIR/${DESKTOP_NAME}" metadata::trusted true 2>/dev/null || true
  fi
  echo "desktop: $DESKTOP_DIR/${DESKTOP_NAME}"
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -f -t "$ICON_DIR" 2>/dev/null || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
fi

echo "menu: $HOME/.local/share/applications/${DESKTOP_NAME}"
echo "done. 从菜单/桌面点 HostChat；Dock 可固定。"
