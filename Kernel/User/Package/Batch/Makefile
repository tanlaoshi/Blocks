# Batch — ToyGfxBatch 范例（PR-GFX-batch-0）
#   make -C User/Package/Batch
#   exec BATCH.ELF

BLOCKS_KERNEL ?= $(abspath ../../..)
PROG      ?= BATCH
SRCS      ?= main.c

EXTRA_LIBS = $(BLOCKS_KERNEL)/User/Library/Graphics/libGraphics.a \
             $(BLOCKS_KERNEL)/User/Library/ToyUi/libToyUi.a

include ../BlocksUser.mk
