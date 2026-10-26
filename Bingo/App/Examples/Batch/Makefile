# Batch — Graphics batch 范例
#   make -C Start/App/Examples/Batch

BLOCKS_SDK ?= $(abspath ../../../../Kernel/Build/BlocksSDK)
PROG      ?= BATCH
SRCS      ?= main.c

EXTRA_LIBS = $(BLOCKS_SDK)/Lib/libGraphics.a \
             $(BLOCKS_SDK)/Lib/libUi.a

include $(BLOCKS_SDK)/BlocksSdk.mk
