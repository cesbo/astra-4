SOURCES="mixaudio.c"
MODULES="mixaudio"

check_cflags()
{
    $APP_C $APP_CFLAGS $1 -x c -o /dev/null -c $MODULE/mixaudio.c >/dev/null 2>&1
}

ffmpeg_configure()
{
    FFMPEG_CONTRIB="$SRCDIR/contrib/build/ffmpeg"

    if ! check_cflags "" ; then
        if [ -d "$FFMPEG_CONTRIB" ] ; then
            CFLAGS="-I$FFMPEG_CONTRIB/"
            LDFLAGS="$FFMPEG_CONTRIB/libavcodec/libavcodec.a $FFMPEG_CONTRIB/libavutil/libavutil.a"
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

ERROR="module is under development"
