# GUI 程序模板（PR-L3）— 链 libUi + libGraphics
#   make -C User/Package/Gui
#   → Build/User/Package/MYGUI/MYGUI.ELF
#   Guest：exec MYGUI.ELF

BLOCKS_KERNEL ?= $(abspath ../../..)
PROG      ?= MYGUI
SRCS      ?= main.c

EXTRA_LIBS = $(BLOCKS_KERNEL)/User/Library/Ui/libUi.a \
             $(BLOCKS_KERNEL)/User/Library/Graphics/libGraphics.a

include $(BLOCKS_KERNEL)/User/Package/BlocksUser.mk
