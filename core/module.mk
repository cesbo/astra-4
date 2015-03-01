
SOURCES="event.c list.c log.c socket.c thread.c timer.c utils.c"

clock_gettime_test_c()
{
    cat <<EOF
#include <time.h>
int main(void) {
    struct timespec ts;
    return clock_gettime(CLOCK_REALTIME, &ts);
}
EOF
}

check_clock_gettime()
{
    clock_gettime_test_c | $APP_C -Werror $CFLAGS $APP_CFLAGS -o /dev/null -lrt -x c - >/dev/null 2>&1
}

if check_clock_gettime ; then
    CFLAGS="$CFLAGS -DHAVE_CLOCK_GETTIME=1"
    LDFLAGS="-lrt"
fi

sctp_h_test_c()
{
    cat <<EOF
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/sctp.h>
int main(void) { return 0; }
EOF
}

check_sctp_h()
{
    sctp_h_test_c | $APP_C -Werror $CFLAGS $APP_CFLAGS -o /dev/null -x c - >/dev/null 2>&1
}

if check_sctp_h ; then
    CFLAGS="$CFLAGS -DHAVE_SCTP_H=1"
fi

endian_h_test_c()
{
    cat <<EOF
#include <endian.h>
#ifndef __BYTE_ORDER
#error "__BYTE_ORDER not defined"
#endif
int main(void) { return 0; }
EOF
}

check_endian_h()
{
    endian_h_test_c | $APP_C -Werror $CFLAGS $APP_CFLAGS -o /dev/null -x c - >/dev/null 2>&1
}

if check_endian_h ; then
    CFLAGS="$CFLAGS -DHAVE_ENDIAN_H=1"
fi
