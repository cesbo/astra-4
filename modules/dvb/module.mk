SOURCES="input.c"
MODULES="dvb_input"

if [ "$IN_TARGET" != "linux" ] ; then
    echo "ERROR: [dvb] module for linux only"
    IS_ERROR=1
fi
