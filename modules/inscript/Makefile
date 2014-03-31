
MAKEFLAGS = -rR --no-print-directory

.PHONY: all

all: inscript.h

inscript.h: ../../scripts/stream.lua
	@gcc -Wall -O2 -std=iso9899:1999 -DINSCRIPT_APP=1 -o inscript inscript.c
	@./inscript stream $< >$@
	@rm inscript
