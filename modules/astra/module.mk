
SOURCES="module_lua.c module_stream.c strhex.c crc32b.c"

SOURCES="$SOURCES astra.c log.c timer.c utils.c"
MODULES="astra log timer utils"

if [ "$OS" != "mingw" ] ; then
    SOURCES="$SOURCES pidfile.c"
    MODULES="$MODULES pidfile"
fi
