
MAKEFLAGS = -rR --no-print-directory

SCRIPTS = ../../scripts
LUA_BASE = $(SCRIPTS)/base.lua
LUA_STREAM = $(SCRIPTS)/stream.lua
LUA_RELAY = $(SCRIPTS)/relay.lua
LUA_ANALYZE = $(SCRIPTS)/analyze.lua
LUA_DVBLS = $(SCRIPTS)/dvbls.lua
LUA_FEMON = $(SCRIPTS)/femon.lua

LUA_ALL = $(LUA_BASE) $(LUA_STREAM) $(LUA_XPROXY) $(LUA_ANALYZE) $(LUA_DVBLS) $(LUA_FEMON)

.PHONY: all

all: inscript.h

inscript.h: $(LUA_ALL)
	@gcc -Wall -O2 -std=iso9899:1999 -DINSCRIPT_APP=1 -o inscript inscript.c
	@rm -f $@
	@./inscript base $(LUA_BASE) >>$@
	@./inscript stream $(LUA_STREAM) >>$@
	@./inscript relay $(LUA_RELAY) >>$@
	@./inscript analyze $(LUA_ANALYZE) >>$@
	@./inscript dvbls $(LUA_DVBLS) >>$@
	@./inscript femon $(LUA_FEMON) >>$@
	@rm inscript
