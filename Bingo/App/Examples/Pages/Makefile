# Pages — 多页 App 范例
#   make -C Start/App/Examples/Pages

BLOCKS_SDK ?= $(abspath ../../../../Kernel/Build/BlocksSDK)
PROG      ?= PAGES
SRCS      ?= main.c Route.c Home.c Settings.c About.c

EXTRA_LIBS = $(BLOCKS_SDK)/Lib/libUi.a \
             $(BLOCKS_SDK)/Lib/libGraphics.a

include $(BLOCKS_SDK)/BlocksSdk.mk
