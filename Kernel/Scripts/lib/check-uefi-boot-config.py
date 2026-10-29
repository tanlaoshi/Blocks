#!/usr/bin/env python3
"""
check-uefi-boot-config.py — Boot / Kernel 两份 UefiBootConfig.h 的一致性检查

两份副本必须各自可编（一份吃 EDK2 头，一份吃内核自己的类型），所以不能合并。
size/offset 断言能挡布局漂移，但挡不住「尺寸不变、语义变了」：
比如把某个 UINT32 从宽改成高，656 字节一个不多一个不少，断言全绿，
内核却按错语义读。这个脚本比对「去掉各自前置段之后」的内容。

用法：
    python3 Kernel/Scripts/lib/check-uefi-boot-config.py [仓库根]

退出码：0 一致；1 不一致（打印 unified diff）。
"""
import difflib
import os
import sys


def normalize(path):
    """剥掉各自独有/形式不同的行，只留宏与结构体定义。"""
    out = []
    in_block_comment = False
    with open(path, encoding='utf-8') as fh:
        for raw in fh:
            line = raw.rstrip('\n')
            stripped = line.strip()

            # 块注释（两种副本里都可能出现）
            if in_block_comment:
                if '*/' in stripped:
                    in_block_comment = False
                continue
            if stripped.startswith('/*'):
                if '*/' not in stripped:
                    in_block_comment = True
                continue
            if stripped.startswith('//'):
                continue

            # 各自独有的前置段与断言：include、断言、断言外面的 #if/#endif 壳
            if stripped.startswith('#include'):
                continue
            if 'assert' in stripped.lower():
                continue
            if stripped in ('#if defined(__GNUC__)', '#endif'):
                continue
            if not stripped:
                continue

            # 空白归一：字段对齐用的空格不影响语义
            out.append(' '.join(stripped.split()))
    return out


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    a = os.path.join(root, 'Boot/BootPkg/UefiBootConfig.h')
    b = os.path.join(root, 'Kernel/BringUp/X64/UefiBootConfig.h')

    for p in (a, b):
        if not os.path.exists(p):
            print('check-uefi-boot-config: 找不到 %s' % p, file=sys.stderr)
            return 1

    la = normalize(a)
    lb = normalize(b)
    if la == lb:
        return 0

    print('check-uefi-boot-config: 两份 UefiBootConfig.h 不一致', file=sys.stderr)
    print('  Boot  : %s' % a, file=sys.stderr)
    print('  Kernel: %s' % b, file=sys.stderr)
    print('  改一份必须同时改另一份；下面的差异以 Boot 为基准、「+」是 Kernel 侧。',
          file=sys.stderr)
    diff = difflib.unified_diff(la, lb, fromfile='Boot', tofile='Kernel',
                                lineterm='', n=2)
    for line in diff:
        print('  ' + line, file=sys.stderr)
    return 1


if __name__ == '__main__':
    sys.exit(main())
