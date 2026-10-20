# GUI 程序模板（PR-L3）— 链 libToyUi + libGraphics
#   make -C User/Package/Gui
#   cp build/MYGUI.ELF ../../ToyImage/rootfs/
#   toyos> exec MYGUI.ELF

BLOCKS_KERNEL ?= $(abspath ../../..)
PROG      ?= MYGUI
SRCS      ?= main.c

EXTRA_LIBS = $(BLOCKS_KERNEL)/User/Library/ToyUi/libToyUi.a \
             $(BLOCKS_KERNEL)/User/Library/Graphics/libGraphics.a

include $(BLOCKS_KERNEL)/User/Package/BlocksUser.mk
