
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
    CFLAGS="-DHAVE_CLOCK_GETTIME=1"
    LDFLAGS="-lrt"
fi
