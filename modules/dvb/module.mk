SOURCES="fe.c input.c"
MODULES="dvb_input"

if [ "$OS" != "linux" ] ; then
    ERROR="Linux required"
fi
