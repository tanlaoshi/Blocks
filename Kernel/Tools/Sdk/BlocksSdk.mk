# BlocksSdk.mk — 离树 SDK 共用规则（由 Tools/build-sdk.sh 打进包根）
# 用法：设 BLOCKS_SDK 指向解压后的 BlocksSDK 根，再 include 本文件。
#
# 调用方需提供：
#   PROG   — 产物基名（默认 MYAPP；盘上建议大写 8.3：MYAPP.ELF）
#   SRCS   — .c 源列表（默认 main.c）
# 可选：
#   EXTRA_LIBS — 额外 .a（包内 Lib/libUi.a 等，写绝对路径或 $(BLOCKS_SDK)/Lib/…）
#   EXTRA_OBJS — 额外 .o

ifndef BLOCKS_SDK
BLOCKS_SDK := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
endif

PROG      ?= MYAPP
SRCS      ?= main.c
BUILDDIR  ?= build
ELF       := $(BUILDDIR)/$(PROG).ELF

ARCH      ?= x86_64
CC        ?= gcc
LD        ?= ld

USER_INC  := $(BLOCKS_SDK)/Include
USER_LD   := $(BLOCKS_SDK)/Link/user.ld
LIB_DIR   := $(BLOCKS_SDK)/Lib

# 与内核自有树同锁 ISO C99 + pedantic；勿链宿主 libc / -pie
USER_CFLAGS ?= -std=c99 -pedantic -ffreestanding -nostdlib -O2 -Wall -Wextra -fno-stack-protector \
	-fno-builtin -fno-pie -fno-pic -m64 -mno-red-zone \
	-I$(USER_INC) -I$(USER_INC)/Abi -I.

CRT0_OBJ     := $(LIB_DIR)/crt0.o
SYSCALL_OBJ  := $(LIB_DIR)/syscall.o
LIBBLOCKS_A  := $(LIB_DIR)/libBlocks.a

OBJS := $(addprefix $(BUILDDIR)/,$(SRCS:.c=.o))

.PHONY: all clean

all: $(ELF)

$(BUILDDIR):
	mkdir -p $@

$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(ELF): $(OBJS) $(CRT0_OBJ) $(SYSCALL_OBJ) $(LIBBLOCKS_A) $(USER_LD) $(EXTRA_OBJS) $(EXTRA_LIBS)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(OBJS) $(EXTRA_OBJS) \
		$(EXTRA_LIBS) $(LIBBLOCKS_A) $(CRT0_OBJ) $(SYSCALL_OBJ)

clean:
	rm -rf $(BUILDDIR)
