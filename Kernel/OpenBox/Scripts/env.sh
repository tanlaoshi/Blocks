#!/bin/bash
# 已迁到仓库根：source ~/Blocks/env.sh
# 本文件仅转调，兼容旧路径。
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    echo "error: 请 source 本脚本，或改用: source ~/Blocks/env.sh" >&2
    exit 1
fi
_BlocksStub="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
# shellcheck source=../../../../env.sh
. "$_BlocksStub/env.sh"
unset _BlocksStub
