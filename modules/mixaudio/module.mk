SOURCES="mixaudio.c"
MODULES="mixaudio"
SCRIPTS="mixaudio.lua"

ffmpeg_test_c()
{
    cat <<EOF
#include <libavcodec/avcodec.h>
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(54,59,100)
#   error "libavcodec >=54.59.100 required (ffmpeg-1.0.7)"
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
    if which pkg-config >/dev/null 2>&1 ; then
        if pkg-config libavcodec ; then
            return 0
        fi
    fi

    return 1
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
            return 1
        fi
    fi

    if ! check_ldflags "$CFLAGS" ; then
        if [ -d "$FFMPEG_CONTRIB" ] ; then
            LDFLAGS="$FFMPEG_CONTRIB/libavcodec/libavcodec.a $FFMPEG_CONTRIB/libavutil/libavutil.a"
        elif check_pkg_config ; then
            LDFLAGS=`pkg-config --libs libavcodec`
        else
            return 1
        fi
    fi
}

if ! ffmpeg_configure ; then
    ERROR="libavcodec is not found. use contrib/ffmpeg.sh"
fi
