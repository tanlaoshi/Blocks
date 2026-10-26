# GuiImmediate — 即时模式 GUI 范例
#   make -C Start/App/Examples/GuiImmediate

BLOCKS_SDK ?= $(abspath ../../../../Kernel/Build/BlocksSDK)
PROG      ?= GUIIM
SRCS      ?= main.c

EXTRA_LIBS = $(BLOCKS_SDK)/Lib/libUi.a \
             $(BLOCKS_SDK)/Lib/libGraphics.a

include $(BLOCKS_SDK)/BlocksSdk.mk
