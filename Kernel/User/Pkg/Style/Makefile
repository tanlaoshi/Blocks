# Style — ToyUiStyle 范例（PR-UI-style-0）
#   make -C User/Pkg/Style
#   exec STYLE.ELF

TOYKERNEL ?= $(abspath ../../..)
PROG      ?= STYLE
SRCS      ?= main.c

EXTRA_LIBS = $(TOYKERNEL)/User/Library/ToyUi/libToyUi.a \
             $(TOYKERNEL)/User/Library/ToyGfx/libToyGfx.a

include ../ToyUser.mk
