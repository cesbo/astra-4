SOURCES_CSA="FFdecsa/FFdecsa.c"
SOURCES_CAM="cam/cam.c cam/newcamd.c"
SOURCES_CAS="cas/irdeto.c cas/viaccess.c cas/dre.c cas/conax.c cas/nagra.c cas/videoguard.c cas/mediaguard.c cas/cryptoworks.c cas/bulcrypt.c cas/exset.c"
SOURCES="$SOURCES_CSA $SOURCES_CAM $SOURCES_CAS decrypt.c"

MODULES="decrypt newcamd"

CFLAGS="-funroll-loops --param max-unrolled-insns=500"
if [ "$OS" = "darwin" ] ; then
    CFLAGS="$CFLAGS -Wno-deprecated-declarations"
fi
LDFLAGS="-lcrypto"

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
    libssl_test_c | $APP_C -Werror $CFLAGS $IN_CFLAGS $LDFLAGS $IN_LDFLAGS -o /dev/null -x c - >/dev/null 2>&1
}

if ! check_libssl ; then
    ERROR="libssl-dev is not found"
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
    sse2_test_c | $APP_C -Werror $CFLAGS $IN_CFLAGS $LDFLAGS $IN_LDFLAGS -o /dev/null -x c - >/dev/null 2>&1
}

# MMX

mmx_test_c()
{
    cat <<EOF
#include <stdio.h>
#include <mmintrin.h>
int main(void) { return 0; }
EOF
}

check_mmx()
{
    mmx_test_c | $APP_C -Werror $CFLAGS $IN_CFLAGS $LDFLAGS $IN_LDFLAGS -o /dev/null -x c - >/dev/null 2>&1
}

if check_sse2 ; then
    CFLAGS="$CFLAGS -DPARALLEL_MODE=1286"
elif check_mmx ; then
    CFLAGS="$CFLAGS -DPARALLEL_MODE=1284"
else
    CFLAGS="$CFLAGS -DPARALLEL_MODE=642"
fi
