
libssl_test_c()
{
    cat <<EOF
#include <stdio.h>
#include <openssl/des.h>
int main(void) { return 0; }
EOF
}

check_libssl()
{
    libssl_test_c | $APP_C -Werror $CFLAGS $APP_CFLAGS -o /dev/null -x c - >/dev/null 2>&1
}

SOURCES_CSA="FFdecsa/FFdecsa.c"
SOURCES_CAM="cam/cam.c"
SOURCES_CAS="cas/irdeto.c cas/viaccess.c cas/dre.c cas/conax.c cas/nagra.c cas/videoguard.c cas/mediaguard.c cas/cryptoworks.c cas/bulcrypt.c cas/exset.c cas/griffin.c"

MODULES="decrypt"

if check_libssl ; then
    LDFLAGS="-lcrypto"
    SOURCES_CAM="$SOURCES_CAM cam/newcamd.c"
    MODULES="$MODULES newcamd"
else
    echo "$MODULE: warning: libssl-dev is not found. newcamd disabled" >&2
fi

SOURCES="$SOURCES_CSA $SOURCES_CAM $SOURCES_CAS decrypt.c"

CFLAGS="-funroll-loops --param max-unrolled-insns=500"
if [ "$OS" = "darwin" ] ; then
    CFLAGS="$CFLAGS -Wno-deprecated-declarations"
fi

# SSE2

sse2_test_c()
{
    cat <<EOF
#include <stdio.h>
#include <emmintrin.h>
int main(void) { return 0; }
EOF
}

check_sse2()
{
    sse2_test_c | $APP_C -Werror $CFLAGS $APP_CFLAGS -o /dev/null -x c - >/dev/null 2>&1
}

if check_sse2 ; then
    CFLAGS="$CFLAGS -DPARALLEL_MODE=1286"
else
    echo "$MODULE: warning: SSE2 is not found" >&2
    CFLAGS="$CFLAGS -DPARALLEL_MODE=642"
fi
