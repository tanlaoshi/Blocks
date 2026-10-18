# Pages — 2 页 App（PR-UI-page-0）
#   make -C User/Package/Pages
#   exec PAGES.ELF

TOYKERNEL ?= $(abspath ../../..)
PROG      ?= PAGES
SRCS      ?= main.c Route.c Home.c Settings.c About.c

EXTRA_LIBS = $(TOYKERNEL)/User/Library/ToyUi/libToyUi.a \
             $(TOYKERNEL)/User/Library/ToyGfx/libToyGfx.a

include ../ToyUser.mk
