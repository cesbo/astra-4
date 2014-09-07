
if [ $LIBDVBCSA -ne 1 ] ; then
    ERROR="libdvbcsa is not found. use --with-libdvbcsa option"
fi

SOURCES="biss_encrypt.c"
MODULES="biss_encrypt"
