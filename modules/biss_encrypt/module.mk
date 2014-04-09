
SOURCES="dvbcsa/dvbcsa_block.c dvbcsa/dvbcsa_key.c \
dvbcsa/dvbcsa_bs_algo.c dvbcsa/dvbcsa_bs_block.c \
dvbcsa/dvbcsa_bs_key.c dvbcsa/dvbcsa_bs_stream.c \
dvbcsa/dvbcsa_bs_transpose.c dvbcsa/dvbcsa_bs_transpose128.c \
biss_encrypt.c"

MODULES="biss_encrypt"

posix_memalign_test_c()
{
    cat <<EOF
#include <stdio.h>
#include <stdlib.h>
int main(void) { void *p = NULL; return posix_memalign(&p, 32, 128); }
EOF
}

check_posix_memalign()
{
    posix_memalign_test_c | $APP_C -Werror $CFLAGS $APP_CFLAGS -o /dev/null -x c - >/dev/null 2>&1
}

if check_posix_memalign ; then
    CFLAGS="-DHAVE_POSIX_MEMALIGN=1"
fi

