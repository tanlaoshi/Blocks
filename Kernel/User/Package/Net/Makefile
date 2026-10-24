# libNet 客户端模板
# 默认内核已 LWIP=1；关栈用 ./build.sh LWIP=0。Guest：lwip on 后 exec NETLIB.ELF / MYNET.ELF

BLOCKS_KERNEL ?= $(abspath ../../..)
PROG      ?= MYNET
SRCS      ?= main.c

EXTRA_LIBS = $(BLOCKS_KERNEL)/User/Library/Net/libNet.a

include $(BLOCKS_KERNEL)/User/Package/BlocksUser.mk
