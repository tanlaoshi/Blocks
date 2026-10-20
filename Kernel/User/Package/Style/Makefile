# Style — ToyUiStyle 范例（PR-UI-style-0）
#   make -C User/Package/Style
#   exec STYLE.ELF

BLOCKS_KERNEL ?= $(abspath ../../..)
PROG      ?= STYLE
SRCS      ?= main.c

EXTRA_LIBS = $(BLOCKS_KERNEL)/User/Library/ToyUi/libToyUi.a \
             $(BLOCKS_KERNEL)/User/Library/Graphics/libGraphics.a

include ../BlocksUser.mk
