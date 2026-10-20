# GUI 程序模板（PR-L3）— 链 libToyUi + libGraphics
#   make -C User/Package/Gui
#   cp build/MYGUI.ELF ../../ToyImage/rootfs/
#   toyos> exec MYGUI.ELF

TOYKERNEL ?= $(abspath ../../..)
PROG      ?= MYGUI
SRCS      ?= main.c

EXTRA_LIBS = $(TOYKERNEL)/User/Library/ToyUi/libToyUi.a \
             $(TOYKERNEL)/User/Library/Graphics/libGraphics.a

include $(TOYKERNEL)/User/Package/ToyUser.mk
