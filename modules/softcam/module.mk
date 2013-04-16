SOURCES_CSA="FFdecsa/FFdecsa.c"
SOURCES_CAM="cam/newcamd.c"
SOURCES_CAS="cas/cas.c cas/template.c"
SOURCES="$SOURCES_CSA $SOURCES_CAM $SOURCES_CAS decrypt.c"

MODULES="decrypt"

CFLAGS="-DPARALLEL_MODE=1286 -funroll-loops --param max-unrolled-insns=500"
if [ "$OS" = "darwin" ] ; then
    CFLAGS="$CFLAGS -Wno-deprecated-declarations"
fi

LDFLAGS="-lcrypto"
