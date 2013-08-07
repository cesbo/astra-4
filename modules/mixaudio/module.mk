SOURCES="mixaudio.c"
MODULES="mixaudio"

ffmpeg_test_c()
{
    cat <<EOF
#include <libavcodec/avcodec.h>
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53,23,0)
#   error "libavcodec >=53.23.0 required"
#else
int main(void) { avcodec_version(); return 0; }
#endif
EOF
}

check_cflags()
{
    ffmpeg_test_c | $APP_C -Werror $APP_CFLAGS $1 -c -o /dev/null -x c - >/dev/null 2>&1
}

check_ldflags()
{
    ffmpeg_test_c | $APP_C -Werror $APP_CFLAGS $1 $APP_LDFLAGS -o /dev/null -x c - >/dev/null 2>&1
}

check_pkg_config()
{
    if ! which pkg-config >/dev/null 2>&1 ; then
        echo "ERROR: [mixaudio] pkg-config is not found. use CFLAGS, LDFLAGS/LIBS"
        return 1
    fi
    if ! pkg-config libavcodec ; then
        echo "ERROR: [mixaudio] libavcodec is not found"
        return 1
    fi
    return 0
}

ffmpeg_configure()
{
    FFMPEG_CONTRIB="$SRCDIR/contrib/build/ffmpeg"

    if ! check_cflags ; then
        if [ -d "$FFMPEG_CONTRIB" ] ; then
            CFLAGS="-I$FFMPEG_CONTRIB/"
        elif check_pkg_config ; then
            CFLAGS=`pkg-config --cflags libavcodec`
        else
            IS_ERROR=1
            return
        fi
    fi

    if ! check_ldflags "$CFLAGS" ; then
        if [ -d "$FFMPEG_CONTRIB" ] ; then
            LDFLAGS="$FFMPEG_CONTRIB/libavcodec/libavcodec.a $FFMPEG_CONTRIB/libavutil/libavutil.a"
        elif check_pkg_config ; then
            LDFLAGS=`pkg-config --libs libavcodec`
        else
            IS_ERROR=1
            return
        fi
    fi
}

ffmpeg_configure
