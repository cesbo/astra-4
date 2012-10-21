#!/bin/bash

if [ $# -eq 0 ] ; then
    echo "Usage: $0 [options] message [files]"
    echo "    --release         release version"
    exit 1
fi

CDIR=`pwd`
cd `dirname $0`

ASTRA_VERSION=`cat version.h | grep "ASTRA_VERSION " | cut -d ' ' -f 3`
ASTRA_VERSION_DEV=`cat version.h | grep "ASTRA_VERSION_DEV " | cut -d ' ' -f 3`

if [ "$1" = "--release" ] ; then
    shift
    ASTRA_VERSION=$((ASTRA_VERSION+1))
    ASTRA_VERSION_DEV=0
else
    ASTRA_VERSION_DEV=$((ASTRA_VERSION_DEV+1))
fi

cat >version.h <<EOF
#ifndef _VERSION_H_
#define _VERSION_H_ 1

#define ASTRA_VERSION $ASTRA_VERSION
#define ASTRA_VERSION_DEV $ASTRA_VERSION_DEV

#endif /* _VERSION_H_ */
EOF

MSG=$1
shift
FLIST=""
if [ $# -ne 0 ] ; then
    FLIST="$* version.h"
fi

hg commit -m "$MSG" $FLIST
git commit -am "$MSG" $FLIST

cd $CDIR
