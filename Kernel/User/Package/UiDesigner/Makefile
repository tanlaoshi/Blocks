# UiDesigner — 设计器导出的 .uitxt → MYAPP.ELF
#   make -C User/Package/UiDesigner

TOYKERNEL ?= $(abspath ../../..)
PROG      ?= MYAPP
SRCS      ?= main.c UiActions.c
UITXT     ?= MyApp.uitxt
UIH       ?= MyAppUi.h
BUILDDIR  ?= build

LAYOUT_C  := $(TOYKERNEL)/User/Library/ToyUi/Layout.c
LAYOUT_O  := $(BUILDDIR)/ToyUiLayout.o

EXTRA_LIBS = $(TOYKERNEL)/User/Library/ToyUi/libToyUi.a \
             $(TOYKERNEL)/User/Library/ToyGfx/libToyGfx.a
EXTRA_OBJS = $(LAYOUT_O)

include ../ToyUser.mk

$(LAYOUT_O): $(LAYOUT_C) $(TOYKERNEL)/User/include/ToyUiLayout.h | $(BUILDDIR)
	$(CC) $(USER_CFLAGS) -c $(LAYOUT_C) -o $@

$(BUILDDIR)/main.o $(BUILDDIR)/UiActions.o: $(UIH)
