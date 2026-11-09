#!/bin/bash
# 准备 Arm64/RiscV QEMU virt 系统盘：同步到 RootFs/$HAL/，打成 RootFs/$HAL/disk.img
# x86 不走这里（用 RootFs/X64 + run-split.sh）。
# （QEMU fat:rw/vvfat 与 virtio-net 同机时会破坏 TX；N10 改用真 FAT 镜像）
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=blocks-common.sh
. "$SCRIPT_DIR/blocks-common.sh"
IMAGE_ROOT="$(blocks_image_root "$0")"
cd "$IMAGE_ROOT"

ARCH="${VIRT_MAKE_ARCH:-}"
HAL_ARCH="${VIRT_HAL_ARCH:-}"
if [ -z "$HAL_ARCH" ]; then
    case "$ARCH" in
        arm64) HAL_ARCH=Arm64 ;;
        riscv) HAL_ARCH=RiscV ;;
        *)
            echo "error: set VIRT_HAL_ARCH=Arm64|RiscV (or VIRT_MAKE_ARCH)" >&2
            exit 1
            ;;
    esac
fi

ROOT="RootFs/$HAL_ARCH"
IMG="$ROOT/disk.img"
mkdir -p "$ROOT"

# 种子优先 X64 系统盘（共享演示/THEME），缺则本仓 Assets
IMG_ROOT="RootFs/X64"
if [ -d "$IMG_ROOT" ]; then
    for F in BLOCKS.ID THEME.CFG HELLO.ELF CAT.ELF WRITE.ELF \
             SYSHELLO.ELF EXECDEMO.ELF PIPEDEMO.ELF BRKDEMO.ELF KILLDEMO.ELF BLOCKS.DB; do
        if [ -f "$IMG_ROOT/$F" ]; then
            cp -f "$IMG_ROOT/$F" "$ROOT/$F"
        fi
    done
    if [ -d "$IMG_ROOT/Assets" ]; then
        mkdir -p "$ROOT/Assets"
        cp -a "$IMG_ROOT/Assets/." "$ROOT/Assets/"
    fi
    if [ -f "$IMG_ROOT/WALL.BMP" ]; then
        mkdir -p "$ROOT/Assets/Images"
        cp -f "$IMG_ROOT/WALL.BMP" "$ROOT/Assets/Images/WALL.BMP"
    fi
fi
if [ ! -f "$ROOT/Assets/Images/WALL.BMP" ] && [ -f Assets/Images/WALL.BMP ]; then
    mkdir -p "$ROOT/Assets/Images"
    cp -f Assets/Images/WALL.BMP "$ROOT/Assets/Images/WALL.BMP"
fi
if [ ! -f "$ROOT/Assets/Icons/bmp48/SHELL.BMP" ] && [ -d Assets/Icons ]; then
    mkdir -p "$ROOT/Assets/Icons"
    cp -a Assets/Icons/. "$ROOT/Assets/Icons/"
fi
if [ ! -f "$ROOT/Assets/Locale/en.txt" ] && [ -d Assets/Locale ]; then
    mkdir -p "$ROOT/Assets/Locale"
    cp -a Assets/Locale/. "$ROOT/Assets/Locale/"
fi
if [ ! -f "$ROOT/Assets/Fonts/VGA8X16.FNT" ] && [ -d Assets/Fonts ]; then
    mkdir -p "$ROOT/Assets/Fonts"
    cp -a Assets/Fonts/. "$ROOT/Assets/Fonts/"
fi
if [ ! -f "$ROOT/Store/catalog.txt" ] && [ -d Store ]; then
    mkdir -p "$ROOT/Store"
    cp -a Store/. "$ROOT/Store/"
fi
# 本仓 Assets 已是真源
if [ ! -f "$ROOT/Assets/Images/WALL.BMP" ] && [ -f Assets/Images/WALL.BMP ]; then
    mkdir -p "$ROOT/Assets/Images"
    cp -f Assets/Images/WALL.BMP "$ROOT/Assets/Images/WALL.BMP"
fi
if [ ! -f "$ROOT/Assets/Icons/bmp48/SHELL.BMP" ] && [ -d Assets/Icons ]; then
    mkdir -p "$ROOT/Assets/Icons"
    cp -a Assets/Icons/. "$ROOT/Assets/Icons/"
fi
if [ ! -f "$ROOT/Assets/Locale/en.txt" ] && [ -d Assets/Locale ]; then
    mkdir -p "$ROOT/Assets/Locale"
    cp -a Assets/Locale/. "$ROOT/Assets/Locale/"
fi
if [ ! -f "$ROOT/Assets/Fonts/VGA8X16.FNT" ] && [ -d Assets/Fonts ]; then
    mkdir -p "$ROOT/Assets/Fonts"
    cp -a Assets/Fonts/. "$ROOT/Assets/Fonts/"
fi
if [ ! -f "$ROOT/Store/catalog.txt" ] && [ -d Store ]; then
    mkdir -p "$ROOT/Store"
    cp -a Store/. "$ROOT/Store/"
fi
mkdir -p "$ROOT/Assets/Icons" "$ROOT/Assets/Locale" "$ROOT/Assets/Fonts"
mkdir -p "$ROOT/Apps" "$ROOT/Store"
rm -rf "$ROOT/Assets/Store" "$ROOT/StoreCache"
rm -f "$ROOT/WALL.BMP"

# PR-A12：本 arch 用户 HELLO 覆盖盘上的 x86 机型
HELLO_SRC="$(blocks_kernel_build_dir "$(blocks_resolve_root "$0")")/HAL/$HAL_ARCH/user/hello.elf"
if [ -f "$HELLO_SRC" ]; then
    cp -f "$HELLO_SRC" "$ROOT/HELLO.ELF"
elif [ -f "$ROOT/HELLO.ELF" ] && [ -n "$ARCH" ]; then
    : # already staged by build.sh
fi

if [ ! -f "$ROOT/BLOCKS.ID" ]; then
    printf "Blocks root volume\n" > "$ROOT/BLOCKS.ID"
fi
if [ ! -f "$ROOT/THEME.CFG" ]; then
    cat > "$ROOT/THEME.CFG" <<'EOF'
desktop=404040
shell=c0c0c0
font=0
mode=1280x720
EOF
fi

python3 - "$ROOT" "$IMG" <<'PY'
import struct, subprocess, pathlib, sys

src = pathlib.Path(sys.argv[1])
img = pathlib.Path(sys.argv[2])
# virt 冒烟盘（仅 QEMU 镜像，非真机卡）；16G 稀疏 FAT32
size = 16 * 1024 * 1024 * 1024
EOC = 0x0FFFFFFF

with open(img, "wb") as f:
    f.truncate(size)
subprocess.check_call(
    ["mkfs.fat", "-F", "32", "-n", "BLOCKS", str(img)],
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)

class Fat32:
    def __init__(self, path):
        self.f = open(path, "r+b")
        b = self.f.read(512)
        self.bps = struct.unpack_from("<H", b, 11)[0]
        self.spc = b[13]
        self.rsv = struct.unpack_from("<H", b, 14)[0]
        self.nfats = b[16]
        spf16 = struct.unpack_from("<H", b, 22)[0]
        self.spf = spf16 if spf16 != 0 else struct.unpack_from("<I", b, 36)[0]
        self.root_cl = struct.unpack_from("<I", b, 44)[0]
        self.fat_off = self.rsv * self.bps
        self.data_off = self.fat_off + self.nfats * self.spf * self.bps
        self.f.seek(self.fat_off)
        self.fat = bytearray(self.f.read(self.spf * self.bps))
        self._next_free = 2

    def fat_get(self, cl):
        return struct.unpack_from("<I", self.fat, cl * 4)[0] & 0x0FFFFFFF

    def fat_set(self, cl, val):
        cur = struct.unpack_from("<I", self.fat, cl * 4)[0]
        struct.pack_into("<I", self.fat, cl * 4, (cur & 0xF0000000) | (val & 0x0FFFFFFF))

    def alloc_cluster(self):
        n = len(self.fat) // 4
        for _ in range(n - 2):
            if self._next_free >= n:
                self._next_free = 2
            cl = self._next_free
            self._next_free += 1
            if self.fat_get(cl) == 0:
                self.fat_set(cl, EOC)
                return cl
        raise RuntimeError("FAT full")

    def write_fat(self):
        for i in range(self.nfats):
            off = self.fat_off + i * self.spf * self.bps
            self.f.seek(off)
            self.f.write(self.fat)

    def cluster_off(self, cl):
        return self.data_off + (cl - 2) * self.spc * self.bps

    def _chain(self, start):
        clusters = []
        cl = start
        seen = set()
        while 2 <= cl < 0x0FFFFFF8:
            if cl in seen:
                break
            seen.add(cl)
            clusters.append(cl)
            cl = self.fat_get(cl)
        return clusters

    def _read_dir(self, dir_cl):
        clsz = self.spc * self.bps
        data = bytearray()
        for cl in self._chain(dir_cl):
            self.f.seek(self.cluster_off(cl))
            data.extend(self.f.read(clsz))
        return data

    def _write_dir(self, dir_cl, data):
        clsz = self.spc * self.bps
        need = max(1, (len(data) + clsz - 1) // clsz)
        clusters = self._chain(dir_cl)
        while len(clusters) < need:
            ncl = self.alloc_cluster()
            self.fat_set(clusters[-1], ncl)
            clusters.append(ncl)
        self.fat_set(clusters[-1], EOC)
        pad = bytearray(need * clsz)
        pad[: len(data)] = data
        for i, c in enumerate(clusters[:need]):
            self.f.seek(self.cluster_off(c))
            self.f.write(pad[i * clsz : (i + 1) * clsz])

    def _find_slot(self, data):
        for i in range(0, len(data), 32):
            if data[i] in (0, 0xE5):
                return i
        data.extend(b"\x00" * 32)
        return len(data) - 32

    def _name83(self, name):
        p = pathlib.Path(name)
        stem = p.stem.upper()[:8]
        ext = p.suffix.upper()[1:4]
        return stem.ljust(8) + (ext.ljust(3) if ext else "   ")

    def _clust_from_ent(self, e, off):
        lo = struct.unpack_from("<H", e, off + 26)[0]
        hi = struct.unpack_from("<H", e, off + 20)[0]
        return (hi << 16) | lo

    def _clust_to_ent(self, e, off, cl):
        struct.pack_into("<H", e, off + 26, cl & 0xFFFF)
        struct.pack_into("<H", e, off + 20, (cl >> 16) & 0xFFFF)

    def _lookup(self, dir_cl, name83):
        data = self._read_dir(dir_cl)
        target = name83.encode("ascii")
        for i in range(0, len(data), 32):
            if data[i] in (0, 0xE5) or data[i + 11] == 0x0F:
                continue
            if data[i : i + 11] == target:
                return self._clust_from_ent(data, i), data[i + 11]
        return None, None

    def add_dir(self, parent_cl, name):
        name83 = self._name83(name)
        existing, attr = self._lookup(parent_cl, name83)
        if existing is not None:
            if not (attr & 0x10):
                raise RuntimeError(f"{name} exists as file")
            return existing
        cl = self.alloc_cluster()
        clsz = self.spc * self.bps
        ent = bytearray(clsz)
        ent[0:11] = b".          "
        ent[11] = 0x10
        self._clust_to_ent(ent, 0, cl)
        ent[32:43] = b"..         "
        ent[43] = 0x10
        self._clust_to_ent(ent, 32, 0 if parent_cl == self.root_cl else parent_cl)
        self.f.seek(self.cluster_off(cl))
        self.f.write(ent)

        data = self._read_dir(parent_cl)
        slot = self._find_slot(data)
        if slot + 32 > len(data):
            data.extend(b"\x00" * 32)
        e = bytearray(32)
        e[0:11] = name83.encode("ascii")
        e[11] = 0x10
        self._clust_to_ent(e, 0, cl)
        data[slot : slot + 32] = e
        end = max(slot + 32, next((i for i in range(len(data) - 1, -1, -1) if data[i]), 0) + 1)
        end = (end + 31) // 32 * 32
        self._write_dir(parent_cl, data[:end])
        return cl

    def add_file(self, parent_cl, name, data_bytes: bytes):
        name83 = self._name83(name)
        clusters = []
        remain = data_bytes
        clsz = self.spc * self.bps
        if not remain:
            first = 0
        else:
            while True:
                cl = self.alloc_cluster()
                clusters.append(cl)
                chunk = remain[:clsz]
                remain = remain[clsz:]
                self.f.seek(self.cluster_off(cl))
                self.f.write(chunk)
                if len(chunk) < clsz:
                    self.f.write(b"\x00" * (clsz - len(chunk)))
                if not remain:
                    break
            for a, b_ in zip(clusters, clusters[1:]):
                self.fat_set(a, b_)
            self.fat_set(clusters[-1], EOC)
            first = clusters[0]

        dir_data = self._read_dir(parent_cl)
        target = name83.encode("ascii")
        slot = None
        for i in range(0, len(dir_data), 32):
            if dir_data[i] in (0, 0xE5):
                if slot is None:
                    slot = i
                continue
            if dir_data[i + 11] == 0x0F:
                continue
            if dir_data[i : i + 11] == target:
                slot = i
                break
        if slot is None:
            slot = self._find_slot(dir_data)
            if slot + 32 > len(dir_data):
                dir_data.extend(b"\x00" * 32)
        e = bytearray(32)
        e[0:11] = target
        e[11] = 0x20
        self._clust_to_ent(e, 0, first)
        struct.pack_into("<I", e, 28, len(data_bytes))
        dir_data[slot : slot + 32] = e
        end = max(slot + 32, next((i for i in range(len(dir_data) - 1, -1, -1) if dir_data[i]), 0) + 1)
        end = (end + 31) // 32 * 32
        self._write_dir(parent_cl, dir_data[:end])

    def close(self):
        self.write_fat()
        self.f.close()

def pack_tree(fat: Fat32, host: pathlib.Path, parent_cl: int):
    for p in sorted(host.iterdir()):
        if p.name.startswith("."):
            continue
        if p.suffix.lower() == ".img":
            continue
        if p.is_dir():
            cl = fat.add_dir(parent_cl, p.name)
            pack_tree(fat, p, cl)
        elif p.is_file():
            fat.add_file(parent_cl, p.name, p.read_bytes())

fat = Fat32(img)
pack_tree(fat, src, fat.root_cl)
fat.close()
print(f"Packed {img} from {src}/ (FAT32 16G sparse)")
PY

echo "Prepared $ROOT + $IMG:"
ls -la "$ROOT" "$IMG"
find "$ROOT/Assets" -type f 2>/dev/null | sort || true
