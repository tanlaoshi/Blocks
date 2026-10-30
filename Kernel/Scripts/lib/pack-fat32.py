#!/usr/bin/env python3
"""把目录打成 FAT32 镜像，或把镜像里的根文件拷回目录。

x86 QEMU 不用 fat:rw。vvfat 在改 THEME.CFG 大小时会在 array_get 处把进程打掉。
"""
import os
import struct
import subprocess
import sys


def _u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def _u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


class Fat32:
    def __init__(self, path):
        self.f = open(path, "r+b")
        b = self.f.read(512)
        self.bps = _u16(b, 11)
        self.spc = b[13]
        self.rsv = _u16(b, 14)
        self.nfats = b[16]
        self.spf = _u32(b, 36)
        self.root = _u32(b, 44)
        self.fat_off = self.rsv * self.bps
        self.clsz = self.spc * self.bps
        self.data_off = self.fat_off + self.nfats * self.spf * self.bps
        self.f.seek(self.fat_off)
        self.fat = bytearray(self.f.read(self.spf * self.bps))
        self.used_short = set()

    def close(self):
        for i in range(self.nfats):
            self.f.seek(self.fat_off + i * self.spf * self.bps)
            self.f.write(self.fat)
        self.f.close()

    def get(self, cl):
        return _u32(self.fat, cl * 4) & 0x0FFFFFFF

    def set(self, cl, val):
        old = _u32(self.fat, cl * 4)
        struct.pack_into("<I", self.fat, cl * 4, (old & 0xF0000000) | (val & 0x0FFFFFFF))

    def alloc(self):
        for cl in range(2, len(self.fat) // 4):
            if self.get(cl) == 0:
                self.set(cl, 0x0FFFFFFF)
                self.f.seek(self.off(cl))
                self.f.write(b"\x00" * self.clsz)
                return cl
        raise RuntimeError("FAT full")

    def off(self, cl):
        return self.data_off + (cl - 2) * self.clsz

    def read_chain(self, cl):
        data = bytearray()
        seen = set()
        while 2 <= cl < 0x0FFFFFF8:
            if cl in seen:
                break
            seen.add(cl)
            self.f.seek(self.off(cl))
            data.extend(self.f.read(self.clsz))
            cl = self.get(cl)
        return data

    def write_chain(self, cl, data):
        need = max(1, (len(data) + self.clsz - 1) // self.clsz)
        clusters = []
        cur = cl
        while 2 <= cur < 0x0FFFFFF8 and len(clusters) < need + 8:
            clusters.append(cur)
            nxt = self.get(cur)
            if nxt >= 0x0FFFFFF8:
                break
            cur = nxt
        while len(clusters) < need:
            ncl = self.alloc()
            self.set(clusters[-1], ncl)
            clusters.append(ncl)
        self.set(clusters[need - 1], 0x0FFFFFFF)
        pad = bytearray(need * self.clsz)
        pad[: len(data)] = data
        for i, c in enumerate(clusters[:need]):
            self.f.seek(self.off(c))
            self.f.write(pad[i * self.clsz : (i + 1) * self.clsz])

    def short_name(self, name):
        if "." in name and not name.startswith("."):
            stem, ext = name.rsplit(".", 1)
        else:
            stem, ext = name, ""

        def clean(text, n):
            out = []
            for ch in text.upper():
                if ch.isalnum() or ch in " !#$%&'()-@^_`{}~":
                    out.append(ch)
            return ("".join(out) or "FILE")[:n]

        s8 = clean(stem, 8)
        e3 = clean(ext, 3)
        plain = s8.ljust(8) + e3.ljust(3)
        lossy = (
            len(stem) > 8
            or len(ext) > 3
            or stem.upper() != stem
            or ext.upper() != ext
            or clean(stem, 99) != stem.upper()
            or clean(ext, 99) != ext.upper()
        )
        if not lossy and plain not in self.used_short:
            self.used_short.add(plain)
            return plain.encode("ascii")
        n = 1
        while True:
            suffix = "~%d" % n
            cand = (s8[: 8 - len(suffix)] + suffix).ljust(8) + e3.ljust(3)
            if cand not in self.used_short:
                self.used_short.add(cand)
                return cand.encode("ascii")
            n += 1

    def lfn_entries(self, name, short):
        checksum = 0
        for c in short:
            checksum = ((checksum & 1) << 7) + (checksum >> 1) + c
            checksum &= 0xFF
        units = []
        chars = list(name) + ["\x00"]
        while len(chars) % 13:
            chars.append("\uffff")
        slots = [chars[i : i + 13] for i in range(0, len(chars), 13)]
        for i, slot in enumerate(reversed(slots)):
            ent = bytearray(32)
            ent[0] = len(slots) - i
            if i == 0:
                ent[0] |= 0x40
            ent[11] = 0x0F
            ent[13] = checksum
            raw = "".join(slot).encode("utf-16le")
            ent[1:11] = raw[0:10]
            ent[14:26] = raw[10:22]
            ent[28:32] = raw[22:26]
            units.append(ent)
        return b"".join(units)

    def dir_end(self, data):
        for i in range(0, len(data), 32):
            if data[i] == 0:
                return i
        return len(data)

    def place(self, dir_cl, blob):
        data = self.read_chain(dir_cl)
        at = self.dir_end(data)
        need = at + len(blob)
        if need > len(data):
            grown = bytearray(((need + self.clsz - 1) // self.clsz) * self.clsz)
            grown[: len(data)] = data
            data = grown
        data[at : at + len(blob)] = blob
        self.write_chain(dir_cl, data[: max(need, self.dir_end(data) or need)])

    def add_dir(self, parent, name):
        cl = self.alloc()
        ent = bytearray(self.clsz)
        ent[0:11] = b".          "
        ent[11] = 0x10
        struct.pack_into("<I", ent, 20, cl >> 16)
        struct.pack_into("<H", ent, 26, cl & 0xFFFF)
        ent[32:43] = b"..         "
        ent[43] = 0x10
        struct.pack_into("<I", ent, 32 + 20, parent >> 16)
        struct.pack_into("<H", ent, 32 + 26, parent & 0xFFFF)
        self.f.seek(self.off(cl))
        self.f.write(ent)
        short = self.short_name(name)
        raw = bytearray(self.lfn_entries(name, short))
        e = bytearray(32)
        e[0:11] = short
        e[11] = 0x10
        struct.pack_into("<I", e, 20, cl >> 16)
        struct.pack_into("<H", e, 26, cl & 0xFFFF)
        raw.extend(e)
        self.place(parent, raw)
        return cl

    def add_file(self, parent, name, payload):
        first = 0
        if payload:
            clsz = self.clsz
            clusters = []
            rest = payload
            while True:
                cl = self.alloc()
                clusters.append(cl)
                chunk = rest[:clsz]
                rest = rest[clsz:]
                self.f.seek(self.off(cl))
                self.f.write(chunk)
                if len(chunk) < clsz:
                    self.f.write(b"\x00" * (clsz - len(chunk)))
                if not rest:
                    break
            for a, b in zip(clusters, clusters[1:]):
                self.set(a, b)
            self.set(clusters[-1], 0x0FFFFFFF)
            first = clusters[0]
        short = self.short_name(name)
        raw = bytearray(self.lfn_entries(name, short))
        e = bytearray(32)
        e[0:11] = short
        e[11] = 0x20
        struct.pack_into("<I", e, 20, first >> 16)
        struct.pack_into("<H", e, 26, first & 0xFFFF)
        struct.pack_into("<I", e, 28, len(payload))
        raw.extend(e)
        self.place(parent, raw)

    def walk_files(self, dir_cl, prefix, out):
        data = self.read_chain(dir_cl)
        lfn = []
        i = 0
        while i + 32 <= len(data):
            if data[i] == 0:
                break
            if data[i] == 0xE5:
                lfn = []
                i += 32
                continue
            if data[i + 11] == 0x0F:
                lfn.append(data[i : i + 32])
                i += 32
                continue
            name = data[i : i + 11]
            if name in (b".          ", b"..         "):
                lfn = []
                i += 32
                continue
            text = self.decode_lfn(lfn) or name.split(b" ")[0].decode("ascii", "replace")
            cl = _u16(data, i + 26) | (_u16(data, i + 20) << 16)
            size = _u32(data, i + 28)
            attr = data[i + 11]
            path = prefix + text
            if attr & 0x10:
                self.walk_files(cl, path + "/", out)
            else:
                blob = self.read_chain(cl)[:size] if cl >= 2 else b""
                out[path] = blob
            lfn = []
            i += 32

    def decode_lfn(self, slots):
        if not slots:
            return ""
        chars = []
        for ent in reversed(slots):
            for off in (1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30):
                u = _u16(ent, off)
                if u in (0, 0xFFFF):
                    return "".join(chars)
                chars.append(chr(u))
        return "".join(chars)


# FAT32 规范要求簇数 >= 65525（否则应算 FAT16）。32MiB/512B 簇只有
# ~64496 簇，mkfs.vfat -F 32 仍会产出 FAT32 BPB，但 OVMF 的 FAT 驱动
# 拒绝挂载 → Boot 读不到 Kernel.elf 而落 UEFI Shell。
# 64MiB（512B 簇约 129k 簇）稳过阈值。
FAT32_MIN_BYTES = 64 * 1024 * 1024


def pack(src, img):
    src = os.path.abspath(src)
    img = os.path.abspath(img)
    total = 0
    for root, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        for name in files:
            if name.startswith(".") or name.endswith(".img"):
                continue
            total += os.path.getsize(os.path.join(root, name))
    size = max(FAT32_MIN_BYTES,
               ((total + 8 * 1024 * 1024 + 1024 * 1024 - 1) // (1024 * 1024)) * 1024 * 1024)
    with open(img, "wb") as f:
        f.truncate(size)
    subprocess.check_call(
        ["mkfs.vfat", "-F", "32", "-n", "BLOCKS", img],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    fat = Fat32(img)

    def rec(host, parent):
        names = sorted(os.listdir(host))
        for name in names:
            if name.startswith(".") or name.endswith(".img"):
                continue
            path = os.path.join(host, name)
            if os.path.isdir(path):
                cl = fat.add_dir(parent, name)
                rec(path, cl)
            elif os.path.isfile(path):
                with open(path, "rb") as fh:
                    fat.add_file(parent, name, fh.read())

    rec(src, fat.root)
    fat.close()
    print("packed %s (%d bytes) from %s" % (img, size, src))


def pull(img, dest, names):
    fat = Fat32(img)
    found = {}
    fat.walk_files(fat.root, "", found)
    fat.close()
    for name in names:
        if name not in found:
            continue
        path = os.path.join(dest, name)
        with open(path, "wb") as fh:
            fh.write(found[name])
        print("pulled %s (%d bytes)" % (name, len(found[name])))


def main(argv):
    if len(argv) < 2:
        print("usage: pack-fat32.py pack SRC IMG | pull IMG DEST FILE...", file=sys.stderr)
        return 2
    if argv[1] == "pack" and len(argv) == 4:
        pack(argv[2], argv[3])
        return 0
    if argv[1] == "pull" and len(argv) >= 5:
        pull(argv[2], argv[3], argv[4:])
        return 0
    print("usage: pack-fat32.py pack SRC IMG | pull IMG DEST FILE...", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
