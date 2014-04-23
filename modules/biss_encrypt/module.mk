
if [ $LIBDVBCSA -ne 1 ] ; then
    ERROR="libdvbcsa is not found. use contrib/libdvbcsa.sh"
fi

SOURCES="biss_encrypt.c"
MODULES="biss_encrypt"
