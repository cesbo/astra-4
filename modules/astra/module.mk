
SOURCES="module.c strhex.c"

SOURCES="$SOURCES astra.c log.c timer.c utils.c"
MODULES="astra log timer utils"

if [ "$OS" != "mingw" ] ; then
    SOURCES="$SOURCES pidfile.c"
    MODULES="$MODULES pidfile"
fi
