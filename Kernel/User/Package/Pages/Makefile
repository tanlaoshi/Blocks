# Pages — 2 页 App（PR-UI-page-0）
#   make -C User/Package/Pages
#   exec PAGES.ELF

BLOCKS_KERNEL ?= $(abspath ../../..)
PROG      ?= PAGES
SRCS      ?= main.c Route.c Home.c Settings.c About.c

EXTRA_LIBS = $(BLOCKS_KERNEL)/User/Library/ToyUi/libToyUi.a \
             $(BLOCKS_KERNEL)/User/Library/Graphics/libGraphics.a

include ../BlocksUser.mk
