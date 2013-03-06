SOURCES_CSA="FFdecsa/FFdecsa.c"
SOURCES="$SOURCES_CSA decrypt.c"

MODULES="decrypt"

CFLAGS="-DPARALLEL_MODE=1286 -funroll-loops --param max-unrolled-insns=500"
if [ "$IN_TARGET" = "darwin" ] ; then
    CFLAGS="$CFLAGS -Wno-deprecated-declarations"
fi

LDFLAGS="-lcrypto"
