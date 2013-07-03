SOURCES_CSA="FFdecsa/FFdecsa.c"
SOURCES_CAM="cam/cam.c cam/newcamd.c"
SOURCES_CAS="cas/irdeto.c cas/viaccess.c cas/dre.c cas/conax.c cas/nagra.c cas/videoguard.c"
SOURCES="$SOURCES_CSA $SOURCES_CAM $SOURCES_CAS decrypt.c"

MODULES="decrypt newcamd"

CFLAGS="-DPARALLEL_MODE=1286 -funroll-loops --param max-unrolled-insns=500"
if [ "$OS" = "darwin" ] ; then
    CFLAGS="$CFLAGS -Wno-deprecated-declarations"
fi

LDFLAGS="-lcrypto"
