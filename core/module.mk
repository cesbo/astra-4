
SOURCES="clock.c compat.c event.c list.c log.c loopctl.c socket.c strbuffer.c thread.c timer.c"

case "$OS" in
"android")
    CFLAGS="$CLFAGS -DWITH_EPOLL=1"
    ;;
"linux")
    CFLAGS="$CLFAGS -DWITH_EPOLL=1"
    ;;
"freebsd")
    CFLAGS="$CLFAGS -DWITH_KQUEUE=1"
    ;;
"darwin")
    CFLAGS="$CLFAGS -DWITH_KQUEUE=1"
    ;;
"mingw")
    CFLAGS="$CFLAGS -DWITH_SELECT=1"
    ;;
*)
    CFLAGS="$CFLAGS -DWITH_SELECT=1"
    ;;
esac

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
    CFLAGS="$CFLAGS -DHAVE_NETINET_SCTP_H=1"
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

pread_test_c()
{
    cat <<EOF
#include <unistd.h>
int main(void) { char b[256]; return pread(0, b, sizeof(b), 0); }
EOF
}

check_pread()
{
    pread_test_c | $APP_C -Werror $CFLAGS $APP_CFLAGS -o /dev/null -x c - >/dev/null 2>&1
}

if check_pread ; then
    CFLAGS="$CFLAGS -DHAVE_PREAD=1"
fi

strndup_test_c()
{
    cat <<EOF
#include <string.h>
int main(void) { return (strndup("test", 2) != NULL) ? 0 : 1; }
EOF
}

check_strndup()
{
    strndup_test_c | $APP_C -Werror $CFLAGS $APP_CFLAGS -o /dev/null -x c - >/dev/null 2>&1
}

if check_strndup ; then
    CFLAGS="$CFLAGS -DHAVE_STRNDUP=1"
fi

strnlen_test_c()
{
    cat <<EOF
#include <string.h>
int main(void) { return (strnlen("test", 2) == 4) ? 0 : 1; }
EOF
}

check_strnlen()
{
    strnlen_test_c | $APP_C -Werror $CFLAGS $APP_CFLAGS -o /dev/null -x c - >/dev/null 2>&1
}

if check_strnlen ; then
    CFLAGS="$CFLAGS -DHAVE_STRNLEN=1"
fi
