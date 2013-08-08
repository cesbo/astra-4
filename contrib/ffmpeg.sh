#!/bin/sh

cd `dirname $0`

if [ ! -d "build" ] ; then
    mkdir build
fi
cd build

FFV="ffmpeg-1.0.7"
ARC="$FFV.tar.gz"

err()
{
    CDIR=`pwd`
    echo "failed to download FFmpeg"
    echo "please, download $DURL"
    echo "to $CDIR"
    exit 1
}

download()
{
    if [ -d "ffmpeg" ] ; then
        return 0
    fi

    if [ ! -f "$ARC" ] ; then
        DURL="http://ffmpeg.org/releases/$ARC"
        DCMD=""

        if which curl >/dev/null ; then
            DCMD="curl -O"
        elif which wget >/dev/null ; then
            DCMD="wget"
        elif which fetch >/dev/null ; then
            DCMD="fetch"
        else
            err
        fi

        $DCMD $DURL
        if [ $? -ne 0 ] ; then
            err
        fi
    fi

    tar -xf $ARC
    mv $FFV ffmpeg
}

download
cd ffmpeg

if [ -f "version.h" ] ; then
    echo "rebuild ffmpeg"
    make distclean
fi

./configure --disable-ffmpeg --disable-ffplay --disable-ffprobe --disable-ffserver --disable-doc --disable-avdevice --disable-swresample --disable-swscale --disable-postproc --disable-network --disable-everything --enable-encoder=mp2,ac3 --enable-decoder=mp1,mp2,mp3,ac3 --disable-yasm \
&& make
