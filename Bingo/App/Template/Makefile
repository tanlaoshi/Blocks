# 毛坯 Makefile — 加 .c 时改 SRCS；开窗再加 EXTRA_LIBS（见 Examples/Gui）。
# 树内默认：先 cd Kernel && ./Tools/build-sdk.sh
# 离树：make BLOCKS_SDK=/path/to/BlocksSDK

BLOCKS_SDK ?= $(abspath ../../../Kernel/Build/BlocksSDK)
PROG      ?= MYAPP
SRCS      ?= main.c

include $(BLOCKS_SDK)/BlocksSdk.mk
