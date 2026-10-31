#!/usr/bin/env python3
"""ksend.py — 把任意文件增量写进 Duo S 的 SD 卡，然后让板子重启。

默认目标仍是 BLOCKS:Kernel.elf；`--path` 可以把同一个协议指向别的文件
（板端对应 `ksend <路径>`，不给路径就是 Kernel.elf）。

协议（与板包 Ksend/ 同源；线上小端）：
    PC  -> DUO  "ksend\\n" 或 "ksend <目标路径>\\n"（路径走板端 shell 参数）
    DUO -> PC   "KSEND READY size=<旧大小> block=4096 n=<旧块数>\\r\\n"
    DUO -> PC   n 个 u32 旧块 CRC32 + 1 字节 '\\n'
    PC  -> DUO  每块：u32 magic=0x4B53424B, u32 index, u32 len, u32 crc32, data[len]
    DUO -> PC   'A' 已写卡 / 'N' 请重发同一块（最多重发 3 次）
    PC  -> DUO  收尾：u32 magic, u32 0xFFFFFFFF, u32 0, u32 新文件整体 CRC32
    DUO -> PC   "KSEND OK crc=.. written=.. size=.."（随后复位）
                或 "KSEND FAIL ..."（不复位；必要时 "KSEND RESTORED"）

两个实现约定（板子侧同样写死，改这里必须同步改板包）：
  * CRC 用 CRC-32/ISO-HDLC，即 Python zlib.crc32；
  * PC 永远重发**最后一块**（哪怕它没变）：收尾帧里没有 size 字段，板子
    只能用「收到过的最大 index*4096+len」推新文件大小；少了最后一块，
    文件变小或末块没变时会推出错误尺寸。

命令：
    python3 Tools/ksend.py --port /dev/ttyUSB0 --file Kernel/Build/DUO/HAL/RiscV/Kernel.elf
    python3 Tools/ksend.py --port /dev/ttyUSB0 --file SNAKE.ELF --path BLOCKS:SNAKE.ELF
    python3 Tools/ksend.py --port /dev/ttyUSB0 --info
    python3 Tools/ksend.py --port /dev/ttyUSB0 --selftest
    python3 Tools/ksend.py --port /dev/ttyUSB0 --dry-run

退出码：0=KSEND OK；1=KSEND FAIL；2=串口/协议错误；3=本地文件或参数错误。
"""

import argparse
import fcntl
import os
import select
import struct
import sys
import termios
import time
import zlib

KSEND_MAGIC = 0x4B53424B
TRAILER_INDEX = 0xFFFFFFFF
ABORT_INDEX = 0xFFFFFFFE
BLOCK_BYTES = 4096
ACK_WRITTEN = b"A"
ACK_RETRY = b"N"
RETRY_MAX = 3
FAT_WRITE_MAX = 8 * 1024 * 1024
# 板端 Ksend/Private.h 的 KSEND_PATH_BYTES 是 64（含结尾 0）；命令行还会带卷名
TARGET_PATH_MAX = 63
# 板端 shell 按空格切参数：只放可打印 ASCII（0x21..0x7E）
PATH_CHAR_MIN = 0x21
PATH_CHAR_MAX = 0x7E
DEFAULT_TARGET_PATH = "BLOCKS:Kernel.elf"

DEFAULT_ELF_CANDIDATES = (
    "Kernel/Build/DUO/HAL/RiscV/Kernel.elf",
    "Kernel/Build/ksend/HAL/RiscV/Kernel.elf",
    "Kernel/Build/HAL/RiscV/Kernel.elf",
)

EXIT_OK = 0
EXIT_BOARD_FAIL = 1
EXIT_PROTOCOL = 2
EXIT_LOCAL = 3


class KsendError(Exception):
    """串口/协议层错误（退出码 2）。"""


class PortTimeout(KsendError):
    """等板子回话超时。"""


class LocalError(Exception):
    """本地文件/参数错误（退出码 3）。"""


def log(message):
    print(message, file=sys.stderr, flush=True)


def board_line(text):
    """板子的人话/日志行：不参与解析，只转给人看。"""
    print("board| %s" % text, file=sys.stderr, flush=True)


def print_board(text):
    """KSEND 协议行原样打出来（stdout，方便重定向留证）。"""
    print(text, flush=True)


class SerialPort:
    """独占打开串口：O_NOCTTY + raw 8N1，读用 select，写走非阻塞轮询。"""

    def __init__(self, path, baud):
        self.path = path
        self.buffer = bytearray()
        try:
            self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        except OSError as error:
            raise KsendError("cannot open %s: %s" % (path, error))
        try:
            self._configure(baud)
        except Exception:
            os.close(self.fd)
            raise

    def _configure(self, baud):
        speed = getattr(termios, "B%d" % baud, None)
        if speed is None:
            raise KsendError("unsupported baud rate %d" % baud)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = 0  # iflag：不要 XON/XOFF、不要 CR/NL 翻译
        attrs[1] = 0  # oflag
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[3] = 0  # lflag：不要回显、不要行缓冲
        attrs[4] = speed
        attrs[5] = speed
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        termios.tcflush(self.fd, termios.TCIOFLUSH)
        # 独占：别让别的进程（CoolTerm/minicom）同时读走协议字节
        try:
            fcntl.ioctl(self.fd, termios.TIOCEXCL)
        except OSError as error:
            raise KsendError("cannot lock %s exclusively: %s" % (self.path, error))

    def close(self):
        try:
            os.close(self.fd)
        except OSError:
            pass
        self.fd = -1

    def _fill(self, timeout):
        readable, _, _ = select.select([self.fd], [], [], max(0.0, timeout))
        if not readable:
            return False
        chunk = os.read(self.fd, 4096)
        if not chunk:
            # 对端关掉（USB 拔了 / 板子复位）：当错误处理，别在 select 里空转
            raise KsendError("serial port closed (EOF)")
        self.buffer += chunk
        return True

    def write_all(self, data, timeout):
        view = memoryview(data)
        deadline = time.monotonic() + timeout
        while len(view) > 0:
            remain = deadline - time.monotonic()
            if remain <= 0:
                raise PortTimeout("serial write timeout")
            _, writable, _ = select.select([], [self.fd], [], remain)
            if not writable:
                raise PortTimeout("serial write timeout")
            try:
                written = os.write(self.fd, view)
            except BlockingIOError:
                continue
            view = view[written:]

    def read_exact(self, count, timeout):
        deadline = time.monotonic() + timeout
        while len(self.buffer) < count:
            remain = deadline - time.monotonic()
            if remain <= 0:
                raise PortTimeout("timeout: wanted %d B, have %d B"
                                  % (count, len(self.buffer)))
            self._fill(remain)
        data = bytes(self.buffer[:count])
        del self.buffer[:count]
        return data

    def read_line(self, timeout):
        deadline = time.monotonic() + timeout
        while True:
            index = self.buffer.find(b"\n")
            if index >= 0:
                line = bytes(self.buffer[:index])
                del self.buffer[:index + 1]
                return line.rstrip(b"\r")
            remain = deadline - time.monotonic()
            if remain <= 0:
                raise PortTimeout("timeout waiting for a line from the board")
            self._fill(min(remain, 0.2))


def parse_fields(line):
    """'KSEND READY size=1 block=4096 n=1' -> {'size': 1, ...}（只认 key=value）。"""
    fields = {}
    for token in line.split():
        if b"=" not in token:
            continue
        key, value = token.split(b"=", 1)
        fields[key.decode("ascii", "replace")] = value.decode("ascii", "replace")
    return fields


def wait_for_line(port, prefixes, timeout, print_unmatched=True):
    """读到以 prefixes 之一开头的行为止；其它（日志/回显）当垃圾丢掉。"""
    deadline = time.monotonic() + timeout
    while True:
        remain = deadline - time.monotonic()
        if remain <= 0:
            raise PortTimeout("timeout waiting for %s"
                              % "/".join(p.decode("ascii") for p in prefixes))
        line = port.read_line(remain)
        text = line.decode("utf-8", "replace")
        if any(line.startswith(p) for p in prefixes):
            return line, text
        if print_unmatched and text:
            board_line(text)


def field_int(fields, key):
    if key not in fields:
        raise KsendError("board line has no %s= field" % key)
    try:
        return int(fields[key], 10)
    except ValueError:
        raise KsendError("field %s=%r is not decimal" % (key, fields[key]))


def field_hex(fields, key):
    if key not in fields:
        raise KsendError("board line has no %s= field" % key)
    try:
        return int(fields[key], 16) & 0xFFFFFFFF
    except ValueError:
        raise KsendError("field %s=%r is not hex" % (key, fields[key]))


def load_file(path):
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError as error:
        raise LocalError("cannot read %s: %s" % (path, error))
    if len(data) == 0:
        raise LocalError("%s is empty" % path)
    if len(data) > FAT_WRITE_MAX:
        raise LocalError("%s is %d B, FAT write cap is %d B"
                         % (path, len(data), FAT_WRITE_MAX))
    return data


def resolve_file(path):
    if path:
        return path
    for candidate in DEFAULT_ELF_CANDIDATES:
        if os.path.isfile(candidate):
            return candidate
    raise LocalError("no kernel ELF found (tried %s); pass --file"
                     % ", ".join(DEFAULT_ELF_CANDIDATES))


def check_target_path(path):
    """板端 shell 按空格切参数：路径不能含空白/控制字节，也不能超板端缓冲。"""
    if not path:
        raise LocalError("--path is empty")
    if len(path) > TARGET_PATH_MAX:
        raise LocalError("--path is %d chars, board limit is %d"
                         % (len(path), TARGET_PATH_MAX))
    for char in path:
        if ord(char) < PATH_CHAR_MIN or ord(char) > PATH_CHAR_MAX:
            raise LocalError("--path %r has a space/control/non-ASCII byte" % path)
    if path.endswith(":"):
        raise LocalError("--path %r has no file name" % path)
    # 板端备份名 = 同目录 <主名>.BAK（主名换扩展名，长度不缩短）
    name = path.rsplit(":", 1)[-1].rsplit("/", 1)[-1]
    stem = name.rsplit(".", 1)[0] if "." in name else name
    backup_length = len(path) - len(name) + len(stem) + len(".BAK")
    if backup_length > TARGET_PATH_MAX:
        raise LocalError("--path %r: its .BAK name is %d chars, board limit is %d"
                         % (path, backup_length, TARGET_PATH_MAX))
    return path


def block_crcs(data):
    crcs = []
    for offset in range(0, len(data), BLOCK_BYTES):
        crcs.append(zlib.crc32(data[offset:offset + BLOCK_BYTES]) & 0xFFFFFFFF)
    return crcs


def whole_crc(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def diff_blocks(local_crcs, board_crcs):
    """只发不同的块；最后一块永远发（板子靠它推新文件尺寸）。"""
    plan = [index for index in range(len(local_crcs))
            if index >= len(board_crcs) or local_crcs[index] != board_crcs[index]]
    last = len(local_crcs) - 1
    if last >= 0 and last not in plan:
        plan.append(last)
    plan.sort()
    return plan


def send_abort(port):
    try:
        port.write_all(struct.pack("<IIII", KSEND_MAGIC, ABORT_INDEX, 0, 0), 5.0)
    except KsendError:
        pass


def read_ack(port, timeout):
    """等 'A'/'N'；中间混进来的字节（异常时的日志）丢掉。"""
    deadline = time.monotonic() + timeout
    stray = bytearray()
    while True:
        remain = deadline - time.monotonic()
        if remain <= 0:
            raise PortTimeout("timeout waiting for block ack")
        data = port.read_exact(1, remain)
        if data in (ACK_WRITTEN, ACK_RETRY):
            if stray:
                board_line("ignored before ack: %r" % bytes(stray))
            return data
        stray += data
        if len(stray) > 512:
            raise KsendError("no ack after 512 stray bytes: %r" % bytes(stray[:64]))


def read_board_table(port, count, timeout):
    """n 个块 CRC + 1 字节 0x0A；n=0 时那一字节也必须吃掉，否则会错位成块头。"""
    raw = b""
    if count:
        raw = port.read_exact(count * 4, timeout)
    terminator = port.read_exact(1, 5.0)
    if terminator != b"\n":
        raise KsendError("block table did not end with 0x0A (got %r)"
                         % terminator)
    if not count:
        return []
    return list(struct.unpack("<%dI" % count, raw))


def command_info(args):
    port = SerialPort(args.port, args.baud)
    try:
        log("port: %s %d 8N1" % (args.port, args.baud))
        port.write_all(b"ksend info\n", 10.0)
        line, text = wait_for_line(port, [b"KSEND INFO ", b"KSEND FAIL"],
                                   args.ready_timeout)
        print_board(text)
        if line.startswith(b"KSEND FAIL"):
            return EXIT_BOARD_FAIL
        fields = parse_fields(line)
        board_size = field_int(fields, "size")
        board_crc = field_hex(fields, "crc")
        if args.file:
            data = load_file(args.file)
            local_crc = whole_crc(data)
            if local_crc == board_crc and len(data) == board_size:
                log("match: local file is identical to the board's Kernel.elf")
            else:
                log("diff: board %d B crc=%08x, local %d B crc=%08x"
                    % (board_size, board_crc, len(data), local_crc))
                return EXIT_BOARD_FAIL
        return EXIT_OK
    finally:
        port.close()


def command_selftest(args):
    port = SerialPort(args.port, args.baud)
    try:
        log("port: %s %d 8N1" % (args.port, args.baud))
        port.write_all(b"ksend selftest\n", 10.0)
        line, text = wait_for_line(
            port, [b"KSEND SELFTEST OK", b"KSEND SELFTEST FAIL"], 120.0)
        print_board(text)
        if line.startswith(b"KSEND SELFTEST FAIL"):
            return EXIT_BOARD_FAIL
        return EXIT_OK
    finally:
        port.close()


def print_plan(plan, data, local_crcs, board_crcs, board_size):
    sent_bytes = sum(min(BLOCK_BYTES, len(data) - index * BLOCK_BYTES)
                     for index in plan)
    same = len(local_crcs) - len(plan)
    print("board: %d B, %d blocks" % (board_size, len(board_crcs)))
    print("local: %d B, %d blocks" % (len(data), len(local_crcs)))
    print("diff: %d/%d blocks differ, %d B (%d KB) to send"
          % (len(plan), len(local_crcs), sent_bytes, sent_bytes // 1024))
    print("  (unchanged blocks reused from the card: %d)" % same)
    for index in plan[:40]:
        length = min(BLOCK_BYTES, len(data) - index * BLOCK_BYTES)
        print("  block %d offset=%d len=%d crc=%08x"
              % (index, index * BLOCK_BYTES, length, local_crcs[index]))
    if len(plan) > 40:
        print("  ... %d more" % (len(plan) - 40))


def command_transfer(args, dry_run):
    file_path = resolve_file(args.file)
    target = check_target_path(args.path or DEFAULT_TARGET_PATH)
    data = load_file(file_path)
    local_crcs = block_crcs(data)
    local_crc = whole_crc(data)
    log("file: %s" % file_path)
    log("target: %s" % target)
    log("local: %d B, %d blocks, crc=%08x"
        % (len(data), len(local_crcs), local_crc))

    port = SerialPort(args.port, args.baud)
    try:
        log("port: %s %d 8N1" % (args.port, args.baud))
        # 默认路径仍发裸 "ksend"（与旧版字节一致）；换目标时把路径当板端 shell 参数。
        trigger = (b"ksend\n" if target == DEFAULT_TARGET_PATH
                   else ("ksend %s\n" % target).encode("ascii"))
        port.write_all(trigger, 10.0)
        # 备份失败时板子会在 READY 之前就回 FAIL，别干等 ready-timeout
        line, text = wait_for_line(port, [b"KSEND READY ", b"KSEND FAIL"],
                                   args.ready_timeout)
        print_board(text)
        if line.startswith(b"KSEND FAIL"):
            return EXIT_BOARD_FAIL
        fields = parse_fields(line)
        board_size = field_int(fields, "size")
        board_block = field_int(fields, "block")
        board_count = field_int(fields, "n")
        if board_block != BLOCK_BYTES:
            raise KsendError("board block=%d, this tool speaks %d"
                             % (board_block, BLOCK_BYTES))
        if board_count != (board_size + BLOCK_BYTES - 1) // BLOCK_BYTES:
            raise KsendError("board n=%d inconsistent with size=%d"
                             % (board_count, board_size))
        board_crcs = read_board_table(port, board_count, args.data_timeout)
        log("board: %d B, %d blocks" % (board_size, board_count))

        plan = diff_blocks(local_crcs, board_crcs)
        print_plan(plan, data, local_crcs, board_crcs, board_size)

        if dry_run:
            log("dry-run: nothing written; aborting the board's transfer")
            send_abort(port)
            try:
                line, text = wait_for_line(port, [b"KSEND ABORT", b"KSEND FAIL"],
                                           args.ack_timeout)
                print_board(text)
            except KsendError:
                log("board did not confirm the abort (it will time out on its own)")
            return EXIT_OK

        sent_bytes = 0
        refused = None
        try:
            for position, index in enumerate(plan, start=1):
                length = min(BLOCK_BYTES, len(data) - index * BLOCK_BYTES)
                chunk = data[index * BLOCK_BYTES:index * BLOCK_BYTES + length]
                header = struct.pack("<IIII", KSEND_MAGIC, index, length,
                                     local_crcs[index])
                attempt = 0
                while True:
                    port.write_all(header + chunk, args.ack_timeout)
                    ack = read_ack(port, args.ack_timeout)
                    if ack == ACK_WRITTEN:
                        break
                    attempt += 1
                    if attempt > RETRY_MAX:
                        refused = ("block %d refused %d times"
                                   % (index, RETRY_MAX))
                        break
                    log("block %d: board said N, resend %d/%d"
                        % (index, attempt, RETRY_MAX))
                if refused:
                    break
                sent_bytes += length
                print("sent %d/%d blocks (%d KB)"
                      % (position, len(plan), sent_bytes // 1024), flush=True)
        except KeyboardInterrupt:
            log("interrupted: telling the board to abort and restore")
            send_abort(port)
            for _ in range(2):
                try:
                    _, text = wait_for_line(
                        port, [b"KSEND ABORT", b"KSEND FAIL", b"KSEND RESTORED",
                               b"KSEND RESTORE FAIL"], 30.0)
                    print_board(text)
                except KsendError:
                    break
            return 130

        if refused:
            # 板子这时也在走失败路径（会 FAIL + 必要时 RESTORED），把它的原话带回来
            log("%s; waiting for the board's verdict" % refused)
            return wait_for_verdict(port, args.verify_timeout)

        log("blocks done, sending trailer crc=%08x" % local_crc)
        port.write_all(struct.pack("<IIII", KSEND_MAGIC, TRAILER_INDEX, 0,
                                   local_crc), args.ack_timeout)
        return wait_for_verdict(port, args.verify_timeout)
    finally:
        port.close()


def wait_for_verdict(port, timeout):
    """等 KSEND OK/FAIL；FAIL 之后再收一行 RESTORED（回滚可能晚一点）。"""
    line, text = wait_for_line(port, [b"KSEND OK ", b"KSEND FAIL"], timeout)
    print_board(text)
    if line.startswith(b"KSEND FAIL"):
        try:
            _, restored = wait_for_line(
                port, [b"KSEND RESTORED", b"KSEND RESTORE FAIL"], 180.0)
            print_board(restored)
        except KsendError:
            log("no restore line from the board (see FAIL reason above)")
        return EXIT_BOARD_FAIL
    return EXIT_OK


def build_parser():
    parser = argparse.ArgumentParser(
        description="Incremental file write to a Milk-V Duo S over serial")
    parser.add_argument("--port", required=True, help="serial device, e.g. /dev/ttyUSB0")
    # --elf 是旧名，保留为 --file 的兼容别名（同一 dest）
    parser.add_argument("--file", "--elf", dest="file",
                        help="local file to send (default: newest known kernel ELF path)")
    parser.add_argument("--path", default=None,
                        help="board-side target path (default: %s)" % DEFAULT_TARGET_PATH)
    parser.add_argument("--baud", type=int, default=115200,
                        help="serial baud (default 115200; the kernel keeps U-Boot's rate)")
    parser.add_argument("--info", action="store_true",
                        help="only print the board's Kernel.elf size/blocks/CRC")
    parser.add_argument("--selftest", action="store_true",
                        help="run the board's 8 KB file-system write self test")
    parser.add_argument("--dry-run", action="store_true",
                        help="handshake and print the diff, then abort without writing")
    parser.add_argument("--ready-timeout", type=float, default=180.0,
                        help="seconds to wait for KSEND READY (backup+CRC happens first)")
    parser.add_argument("--ack-timeout", type=float, default=30.0,
                        help="seconds to wait for a block ack / write")
    parser.add_argument("--data-timeout", type=float, default=30.0,
                        help="seconds to read the block CRC table")
    parser.add_argument("--verify-timeout", type=float, default=300.0,
                        help="seconds to wait for KSEND OK after the trailer")
    return parser


def main(argv):
    args = build_parser().parse_args(argv)
    if sum(1 for flag in (args.info, args.selftest, args.dry_run) if flag) > 1:
        log("--info / --selftest / --dry-run are mutually exclusive")
        return EXIT_LOCAL
    # --info 不带 --file 时只查板子，不和本地文件比
    try:
        if args.info:
            return command_info(args)
        if args.selftest:
            return command_selftest(args)
        return command_transfer(args, dry_run=args.dry_run)
    except LocalError as error:
        log("ksend.py: %s" % error)
        return EXIT_LOCAL
    except KsendError as error:
        log("ksend.py: %s" % error)
        return EXIT_PROTOCOL
    except KeyboardInterrupt:
        log("interrupted")
        return 130


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
