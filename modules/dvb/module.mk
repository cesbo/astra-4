SOURCES="src/fe.c src/dvr.c src/dmx.c src/ca.c"
SOURCES="$SOURCES input.c dvbls.c"
MODULES="dvb_input dvbls"

if [ "$OS" != "linux" ] ; then
    ERROR="Linux required"
fi
