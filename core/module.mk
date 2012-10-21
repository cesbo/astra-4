
SOURCES="event.c list.c log.c module.c socket.c stream.c thread.c timer.c"

PROTOCOLS_C="modules/protocols.c"
if [ "$ARG_MODULES" != "_" -a -f "$SRCDIR/$PROTOCOLS_C" ] ; then
    SOURCES="$SOURCES ../$PROTOCOLS_C"
    # TODO: set "#include "../modules/protocols.h"" into astra.h
fi
