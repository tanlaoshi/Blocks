#!/bin/bash
set -e
cd "$(dirname "$0")"

# Blocks 单仓：产物始终 ./Build（旁挂 Boot/Runtime，不再回写 ToyOS Build）。
# 旧 ToyOS 树根探测已去掉，避免 BLOCKS_ROOT 把成品拐走。
toyos_pick_root_for_build() {
    return 1
}

BUILDDIR=Build
if Root="$(toyos_pick_root_for_build)"; then
    export BLOCKS_ROOT="$Root"
    # 去掉旧仓内 Build 符号链接 / 真目录（有内容则迁到树根）
    if [ -L Build ]; then
        rm -f Build
    elif [ -d Build ]; then
        mkdir -p "$Root/Build/ToyKernel"
        if [ "$(ls -A Build 2>/dev/null)" ]; then
            cp -a Build/. "$Root/Build/ToyKernel"/ 2>/dev/null || true
        fi
        rm -rf Build
    fi
    BUILDDIR="$Root/Build/ToyKernel"
    mkdir -p "$BUILDDIR"
    echo "BUILDDIR=$BUILDDIR"
fi
export BUILDDIR

# 用法:
#   ./build.sh              # ARCH=x86_64
#   ./build.sh sdk          # 打 Build/BlocksSDK.tar.gz（不编内核）
#   ./build.sh DEBUG=1
#   ./build.sh SERIAL=0              # 总开关：不 Init UART、无串口 TX
#   ./build.sh SERIAL_USB=0          # 仅 USB/xHCI 串口 quiet
#   ./build.sh SCREEN_LOG=1          # 打开 boot GOP 上滚（默认关，省 4K blit）
#   ./build.sh FILE_LOG=0            # 不写 BOOT.LOG（默认写）
#   ./build.sh SCREEN_LOG_SMP=1      # 屏上也打 SMP（需先 SCREEN_LOG=1）
#   ./build.sh NO_COM1=1             # 兼容旧名，等价 SERIAL=0
#   ./build.sh arm64        # PR-A7：完整 Common → KernelMain（默认 BRINGUP=0；无 LwIp 端口时自动 LWIP=0）
#   ./build.sh riscv
#   ./build.sh arm64 BRINGUP=1   # PR-A6：仅串口 hello
#   ./build.sh arm64 BOARD=virt
#   ./build.sh arm64 BOARD=rpi-3b-plus LWIP=0
#   ./build.sh riscv BOARD=milk-v-duo-s
#   ./build.sh arm64 LWIP=0      # 显式关（默认已自动关）
#   ./build.sh V=1          # 打印每条 gcc/ld；默认只留警告、错误和结果
#
# 日常推荐（仓库根）：
#   source ~/Blocks/env.sh
#   build arm64 BOARD=rpi-3b-plus LWIP=0
#   run-virt-arm --headless

# 仅打用户态 SDK（离树分发）；其余参数忽略
if [ "${1:-}" = "sdk" ]; then
    exec bash Tools/build-sdk.sh
fi

ARCH=x86_64
BOARD=virt
DEBUG=0
LWIP=1
LWIP_SET=0
NO_COM1=0
SERIAL=1
SERIAL_BOOT=1
SERIAL_USB=1
SERIAL_SMP=1
SERIAL_GUI=1
SERIAL_NET=1
SERIAL_FS=1
SERIAL_MEM=1
SERIAL_DRV=1
SERIAL_MISC=1
SCREEN_LOG=0
FILE_LOG=1
# 下列为 SCREEN_LOG=1 时的课堂默认；总关时 Makefile/脚本会强制清零
SCREEN_LOG_BOOT=1
SCREEN_LOG_USB=1
SCREEN_LOG_SMP=0
SCREEN_LOG_GUI=1
SCREEN_LOG_NET=0
SCREEN_LOG_FS=1
SCREEN_LOG_MEM=0
SCREEN_LOG_DRV=0
SCREEN_LOG_MISC=0
DEMO_DRIVER=1
KERNEL_TASK_DEMO=0
BRINGUP=
QUIET=1
for Arg in "$@"; do
    case "$Arg" in
        DEBUG=1|debug=1) DEBUG=1 ;;
        DEBUG=0|debug=0) DEBUG=0 ;;
        LWIP=1|lwip=1) LWIP=1; LWIP_SET=1 ;;
        LWIP=0|lwip=0) LWIP=0; LWIP_SET=1 ;;
        NO_COM1=1|no_com1=1) NO_COM1=1; SERIAL=0 ;;
        NO_COM1=0|no_com1=0) NO_COM1=0 ;;
        SERIAL=1|serial=1) SERIAL=1 ;;
        SERIAL=0|serial=0) SERIAL=0 ;;
        SERIAL_BOOT=*) SERIAL_BOOT="${Arg#SERIAL_BOOT=}" ;;
        SERIAL_USB=*) SERIAL_USB="${Arg#SERIAL_USB=}" ;;
        SERIAL_SMP=*) SERIAL_SMP="${Arg#SERIAL_SMP=}" ;;
        SERIAL_GUI=*) SERIAL_GUI="${Arg#SERIAL_GUI=}" ;;
        SERIAL_NET=*) SERIAL_NET="${Arg#SERIAL_NET=}" ;;
        SERIAL_FS=*) SERIAL_FS="${Arg#SERIAL_FS=}" ;;
        SERIAL_MEM=*) SERIAL_MEM="${Arg#SERIAL_MEM=}" ;;
        SERIAL_DRV=*) SERIAL_DRV="${Arg#SERIAL_DRV=}" ;;
        SERIAL_MISC=*) SERIAL_MISC="${Arg#SERIAL_MISC=}" ;;
        SCREEN_LOG=1|screen_log=1) SCREEN_LOG=1 ;;
        SCREEN_LOG=0|screen_log=0) SCREEN_LOG=0 ;;
        FILE_LOG=1|file_log=1) FILE_LOG=1 ;;
        FILE_LOG=0|file_log=0) FILE_LOG=0 ;;
        SCREEN_LOG_BOOT=*) SCREEN_LOG_BOOT="${Arg#SCREEN_LOG_BOOT=}" ;;
        SCREEN_LOG_USB=*) SCREEN_LOG_USB="${Arg#SCREEN_LOG_USB=}" ;;
        SCREEN_LOG_SMP=*) SCREEN_LOG_SMP="${Arg#SCREEN_LOG_SMP=}" ;;
        SCREEN_LOG_GUI=*) SCREEN_LOG_GUI="${Arg#SCREEN_LOG_GUI=}" ;;
        SCREEN_LOG_NET=*) SCREEN_LOG_NET="${Arg#SCREEN_LOG_NET=}" ;;
        SCREEN_LOG_FS=*) SCREEN_LOG_FS="${Arg#SCREEN_LOG_FS=}" ;;
        SCREEN_LOG_MEM=*) SCREEN_LOG_MEM="${Arg#SCREEN_LOG_MEM=}" ;;
        SCREEN_LOG_DRV=*) SCREEN_LOG_DRV="${Arg#SCREEN_LOG_DRV=}" ;;
        SCREEN_LOG_MISC=*) SCREEN_LOG_MISC="${Arg#SCREEN_LOG_MISC=}" ;;
        DEMO_DRIVER=*) DEMO_DRIVER="${Arg#DEMO_DRIVER=}" ;;
        TOY_DEMO_DRIVER=*) DEMO_DRIVER="${Arg#TOY_DEMO_DRIVER=}" ;;
        KERNEL_TASK_DEMO=*) KERNEL_TASK_DEMO="${Arg#KERNEL_TASK_DEMO=}" ;;
        V=1|v=1|VERBOSE=1|verbose=1) QUIET=0 ;;
        V=0|v=0|VERBOSE=0|verbose=0) QUIET=1 ;;
        BRINGUP=1|bringup=1) BRINGUP=1 ;;
        BRINGUP=0|bringup=0) BRINGUP=0 ;;
        BOARD=*) BOARD="${Arg#BOARD=}" ;;
        *) ARCH="$Arg" ;;
    esac
done

if [ -z "$BRINGUP" ]; then
    BRINGUP=0
fi

case "$ARCH" in
    x86_64) HAL_ARCH=X64 ;;
    arm64)  HAL_ARCH=Arm64 ;;
    riscv)  HAL_ARCH=RiscV ;;
    *)
        echo "error: unknown ARCH=$ARCH (expected x86_64|arm64|riscv)" >&2
        exit 1
        ;;
esac

# virt arm64/riscv 尚无 HAL/*/LwIp/lwipopts.h；默认关 lwIP，避免误编挂掉
if [ "$LWIP" = "1" ] && [ ! -f "HAL/$HAL_ARCH/LwIp/include/lwipopts.h" ]; then
    if [ "$LWIP_SET" = "1" ]; then
        echo "error: LWIP=1 but HAL/$HAL_ARCH/LwIp/include/lwipopts.h missing (no port yet)" >&2
        echo "  use: ./build.sh $ARCH LWIP=0" >&2
        exit 1
    fi
    echo "note: ARCH=$ARCH has no LwIp port — using LWIP=0"
    LWIP=0
fi

echo "Building ToyKernel for ARCH=$ARCH BOARD=$BOARD KERNEL_DEBUG=$DEBUG SERIAL=$SERIAL SCREEN_LOG=$SCREEN_LOG FILE_LOG=$FILE_LOG USB=$SERIAL_USB LWIP=$LWIP BRINGUP=$BRINGUP"
ELF="$BUILDDIR/HAL/$HAL_ARCH/Kernel.elf"
USER_HELLO="$BUILDDIR/HAL/$HAL_ARCH/user/hello.elf"

MAKE=(make)
if [ "$QUIET" = 1 ]; then
    MAKE=(make -s)
fi
"${MAKE[@]}" clean ARCH="$ARCH" BOARD="$BOARD" BUILDDIR="$BUILDDIR"
"${MAKE[@]}" ARCH="$ARCH" BOARD="$BOARD" BUILDDIR="$BUILDDIR" DEBUG="$DEBUG" LWIP="$LWIP" BRINGUP="$BRINGUP" \
    NO_COM1="$NO_COM1" SERIAL="$SERIAL" \
    SERIAL_BOOT="$SERIAL_BOOT" SERIAL_USB="$SERIAL_USB" SERIAL_SMP="$SERIAL_SMP" \
    SERIAL_GUI="$SERIAL_GUI" SERIAL_NET="$SERIAL_NET" SERIAL_FS="$SERIAL_FS" \
    SERIAL_MEM="$SERIAL_MEM" SERIAL_DRV="$SERIAL_DRV" SERIAL_MISC="$SERIAL_MISC" \
    SCREEN_LOG="$SCREEN_LOG" FILE_LOG="$FILE_LOG" \
    SCREEN_LOG_BOOT="$SCREEN_LOG_BOOT" SCREEN_LOG_USB="$SCREEN_LOG_USB" \
    SCREEN_LOG_SMP="$SCREEN_LOG_SMP" SCREEN_LOG_GUI="$SCREEN_LOG_GUI" \
    SCREEN_LOG_NET="$SCREEN_LOG_NET" SCREEN_LOG_FS="$SCREEN_LOG_FS" \
    SCREEN_LOG_MEM="$SCREEN_LOG_MEM" SCREEN_LOG_DRV="$SCREEN_LOG_DRV" \
    SCREEN_LOG_MISC="$SCREEN_LOG_MISC" \
    DEMO_DRIVER="$DEMO_DRIVER" \
    KERNEL_TASK_DEMO="$KERNEL_TASK_DEMO"

if [ ! -f "$ELF" ]; then
    echo "Build failed!"
    exit 1
fi

echo "Build successful: $ELF (BOARD=$BOARD DEBUG=$DEBUG LWIP=$LWIP BRINGUP=$BRINGUP)"

# CI 只 checkout 内核，无 ../Runtime；有则同步到 RootFs/$HAL
# PR-LAN-store-src：卷根仅 ROOTFS-ELF 白名单；商店在 Store/（废 Assets/Store + StoreCache）
if [ "$ARCH" = "x86_64" ] && [ "$BRINGUP" = "0" ] && [ -d ../Runtime/RootFs/X64 ]; then
    DEST=../Runtime/RootFs/X64
    IMAGE_STORE=../Runtime/Store
    USER_OUT="$BUILDDIR/User"
    cp -f "$ELF" "$DEST/Kernel.elf"
    # Image 种子：Assets → Guest Assets/
    if [ -d ../Runtime/Assets ]; then
        mkdir -p "$DEST/Assets"
        cp -a ../Runtime/Assets/. "$DEST/Assets/"
        rm -rf "$DEST/Assets/Store"
    fi

    # 上课 ELF → Class/（白名单见 User/Apps/README.md user-apps-1）。exec 裸名再找 Class/。
    mkdir -p "$DEST/Class"
    cp -f "$USER_OUT/hello.elf" "$DEST/Class/HELLO.ELF"
    cp -f "$USER_OUT/count.elf" "$DEST/Class/COUNT.ELF"
    cp -f "$USER_OUT/fork.elf" "$DEST/Class/FORK.ELF"
    cp -f "$USER_OUT/waitnh.elf" "$DEST/Class/WAITNH.ELF"
    cp -f "$USER_OUT/libtoy.so" "$DEST/Class/LIBTOY.SO"
    cp -f "$USER_OUT/dyndemo.elf" "$DEST/Class/DYNDEMO.ELF"
    cp -f "$USER_OUT/syshello.elf" "$DEST/Class/SYSHELLO.ELF"
    cp -f "$USER_OUT/sysfork.elf" "$DEST/Class/SYSFORK.ELF"
    cp -f "$USER_OUT/execdemo.elf" "$DEST/Class/EXECDEMO.ELF"
    cp -f "$USER_OUT/pipedemo.elf" "$DEST/Class/PIPEDEMO.ELF"
    cp -f "$USER_OUT/threadsmoke.elf" "$DEST/Class/THREADSMOKE.ELF"
    cp -f "$USER_OUT/pthreadsmoke.elf" "$DEST/Class/PTHREADSMOKE.ELF"
    cp -f "$USER_OUT/threaddemo.elf" "$DEST/Class/THREADDEMO.ELF"
    cp -f "$USER_OUT/enosysdemo.elf" "$DEST/Class/ENOSYS.ELF"
    # 课上 exec Class/… 直达（与 Store/Apps 预装并存；卷根仍清陈货）
    cp -f "$USER_OUT/sockdemo.elf" "$DEST/Class/SOCKDEMO.ELF"
    cp -f "$USER_OUT/netlibdemo.elf" "$DEST/Class/NETLIB.ELF"
    cp -f "$USER_OUT/dirdemo.elf" "$DEST/Class/DIRDEMO.ELF"
    rm -f "$DEST/Class/GUIDEMO.ELF" "$DEST/Class/WINDEMO.ELF" "$DEST/Class/BLITDEMO.ELF"
    rm -f "$DEST/HELLO.ELF" "$DEST/COUNT.ELF" "$DEST/FORK.ELF" "$DEST/WAITNH.ELF" \
        "$DEST/LIBTOY.SO" "$DEST/DYNDEMO.ELF" "$DEST/SYSHELLO.ELF" "$DEST/SYSFORK.ELF" \
        "$DEST/EXECDEMO.ELF" "$DEST/PIPEDEMO.ELF" "$DEST/THREADSMOKE.ELF" \
        "$DEST/PTHREADSMOKE.ELF" "$DEST/THREADDEMO.ELF" "$DEST/ENOSYS.ELF" \
        "$DEST/MYAPP.ELF"

    # Class/MYAPP.ELF：与 HELLO 同源（毛坯在 Start/App）
    rm -f "$DEST/Class/MYAPP.ELF"
    if [ -f "$USER_OUT/hello.elf" ]; then
        cp -f "$USER_OUT/hello.elf" "$DEST/Class/MYAPP.ELF"
    fi

    # 课堂预装目录包（非扁平）；仍禁止 Apps/*.ELF。Snake 不预装，须 store install。
    mkdir -p "$DEST/Apps/hello" "$DEST/Apps/taskmgr"
    cp -f "$USER_OUT/hello.elf" "$DEST/Apps/hello/HELLO.ELF"
    cp -f "$USER_OUT/taskmgr.elf" "$DEST/Apps/taskmgr/TASKMGR.ELF"
    rm -f "$DEST/Apps/TASKMGR.ELF" "$DEST/Apps/CAT.ELF" "$DEST/Apps/SNAKE.ELF"
    rm -rf "$DEST/Apps/snake" "$DEST/Apps/guidemo"

    PackStore() {
        local Id="$1" Src="$2" File="$3"
        mkdir -p "$IMAGE_STORE/packages/$Id" "$DEST/Store/packages/$Id"
        cp -f "$Src" "$IMAGE_STORE/packages/$Id/$File"
        if [ -f "$IMAGE_STORE/packages/$Id/PKG.TXT" ]; then
            cp -f "$IMAGE_STORE/packages/$Id/PKG.TXT" "$DEST/Store/packages/$Id/"
        fi
        cp -f "$Src" "$DEST/Store/packages/$Id/$File"
        if [ -d "$IMAGE_STORE/packages/$Id/Assets" ]; then
            rm -rf "$DEST/Store/packages/$Id/Assets"
            cp -a "$IMAGE_STORE/packages/$Id/Assets" "$DEST/Store/packages/$Id/"
        fi
    }
    PackStore hello "$USER_OUT/hello.elf" HELLO.ELF
    PackStore cat "$USER_OUT/catfile.elf" CAT.ELF
    PackStore taskmgr "$USER_OUT/taskmgr.elf" TASKMGR.ELF
    PackStore snake "$USER_OUT/snake.elf" SNAKE.ELF
    PackStore chat "$USER_OUT/chat.elf" CHAT.ELF
    if [ -f "$IMAGE_STORE/packages/chat/PKG.TXT" ]; then
        cp -f "$IMAGE_STORE/packages/chat/PKG.TXT" "$DEST/Store/packages/chat/"
    fi
    rm -rf "$IMAGE_STORE/packages/guidemo" "$IMAGE_STORE/packages/windemo" \
        "$IMAGE_STORE/packages/blitdemo" \
        "$DEST/Store/packages/guidemo" "$DEST/Store/packages/windemo" \
        "$DEST/Store/packages/blitdemo"
    if [ -f "$IMAGE_STORE/catalog.txt" ]; then
        mkdir -p "$DEST/Store"
        cp -f "$IMAGE_STORE/catalog.txt" "$DEST/Store/catalog.txt"
    fi
    # 废旧路径（store-src）
    rm -rf "$DEST/Assets/Store" "$DEST/StoreCache"

    # 清非白名单卷根陈货（入店类改走 Store/packages）
    rm -f "$DEST/CAT.ELF" "$DEST/WRITE.ELF" "$DEST/NETDEMO.ELF" "$DEST/NETSRV.ELF" \
        "$DEST/BRKDEMO.ELF" "$DEST/MMAPDEMO.ELF" "$DEST/KILLDEMO.ELF" "$DEST/SIGDEMO.ELF" \
        "$DEST/WINDEMO.ELF" "$DEST/GUIDEMO.ELF" "$DEST/BLITDEMO.ELF" "$DEST/LIBCDEMO.ELF" \
        "$DEST/SLEEPDEMO.ELF" "$DEST/SNAKE.ELF" "$DEST/TASKMGR.ELF" "$DEST/DIRDEMO.ELF" \
        "$DEST/CWDDEMO.ELF" "$DEST/NETLIB.ELF" "$DEST/SOCKDEMO.ELF"

    echo "Synced Kernel + root whitelist + Store/packages + Apps/<id> -> $DEST/"
elif [ "$ARCH" = "x86_64" ] && [ "$BRINGUP" = "0" ]; then
    echo "note: no ../Runtime/RootFs/X64 (CI) — skip demo ELF copy"
else
    echo "Non-x86 / bringup ELF (not copied to Runtime root): $ELF"
    if [ "$BRINGUP" = "0" ] && [ -f "$USER_HELLO" ]; then
        case "$ARCH" in
            arm64) HAL_DIR=Arm64 ;;
            riscv) HAL_DIR=RiscV ;;
            *) HAL_DIR= ;;
        esac
        if [ -n "$HAL_DIR" ] && [ -d "../Runtime/RootFs/$HAL_DIR" ]; then
            cp -f "$USER_HELLO" "../Runtime/RootFs/$HAL_DIR/HELLO.ELF"
            echo "Copied $USER_HELLO -> ../Runtime/RootFs/$HAL_DIR/HELLO.ELF"
        fi
    fi
fi

ls -lh "$ELF"
