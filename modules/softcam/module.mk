SOURCES_CSA="FFdecsa/FFdecsa.c"
SOURCES_CAM="cam/cam.c cam/biss.c cam/newcamd.c"
SOURCES_CAS="cas/cas.c cas/biss.c cas/viaccess.c cas/dre.c cas/irdeto.c"
SOURCES_CAS="$SOURCES_CAS cas/conax.c cas/mediaguard.c cas/nagra.c"
SOURCES_CAS="$SOURCES_CAS cas/bulcrypt.c cas/cryptoworks.c"
SOURCES_CAS="$SOURCES_CAS cas/videoguard.c"
SOURCES="$SOURCES_CSA decrypt.c $SOURCES_CAM $SOURCES_CAS"

MODULES_CAM="biss newcamd"
MODULES="decrypt $MODULES_CAM"

# TODO: check modules/mpegts
echo "TODO: configure FFdecsa (SSE2)"
CFLAGS="-DPARALLEL_MODE=1286 -funroll-loops --param max-unrolled-insns=500"

if [ "$IN_TARGET" = "darwin" ] ; then
    CFLAGS="$CFLAGS -Wno-deprecated-declarations"
fi

LDFLAGS="-lcrypto"
