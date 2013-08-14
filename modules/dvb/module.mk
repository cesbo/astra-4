SOURCES="src/fe.c src/ca.c"
SOURCES="$SOURCES input.c dvbls.c ddci.c"
MODULES="dvb_input dvbls ddci"

if [ "$OS" != "linux" ] ; then
    ERROR="Linux required"
fi
