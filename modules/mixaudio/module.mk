SOURCES="mixaudio.c"
MODULES="mixaudio"

check_cflags()
{
    $APP_C -Werror $APP_CFLAGS $1 -x c -o /dev/null -c $MODULE/mixaudio.c >/dev/null 2>&1
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

    if ! check_cflags "" ; then
        if [ -d "$FFMPEG_CONTRIB" ] ; then
            CFLAGS="-I$FFMPEG_CONTRIB/"
            LDFLAGS="$FFMPEG_CONTRIB/libavcodec/libavcodec.a $FFMPEG_CONTRIB/libavutil/libavutil.a"
        elif check_pkg_config ; then
            CFLAGS=`pkg-config --cflags libavcodec`
            LDFLAGS=`pkg-config --libs libavcodec`
        else
            return 1
        fi

        if ! check_cflags "$CFLAGS" ; then
            return 1
        fi
    fi
}

if ! ffmpeg_configure ; then
    ERROR="libavcodec is not found. use contrib/ffmpeg.sh"
fi
