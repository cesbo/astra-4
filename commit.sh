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

IS_RELEASE=0

if [ "$1" = "--release" ] ; then
    shift
    let ASTRA_VERSION=ASTRA_VERSION+1
    ASTRA_VERSION_DEV=0
    IS_RELEASE=1
else
    let ASTRA_VERSION_DEV=ASTRA_VERSION_DEV+1
fi

cat >version.h <<EOF
#ifndef _VERSION_H_
#define _VERSION_H_ 1

#define ASTRA_VERSION $ASTRA_VERSION
#define ASTRA_VERSION_DEV $ASTRA_VERSION_DEV

#endif /* _VERSION_H_ */
EOF

VERSION="v.$ASTRA_VERSION.$ASTRA_VERSION_DEV"
GITRET=1
MSG=$1
shift

if [ $# -ne 0 ] ; then
    git commit -m "$VERSION $MSG" $* version.h
    GITRET=$?
else
    git commit -am "$VERSION $MSG"
    GITRET=$?
fi

if [ $GITRET -ne 0 ] ; then
    echo "git commit failed"
    git checkout -- version.h
    exit 1
fi

if [ $IS_RELEASE -eq 1 ] ; then
    git checkout master
    git merge --no-ff -m "$VERSION merge" dev
fi

git tag "$VERSION"

if [ $IS_RELEASE -eq 1 ] ; then
    git checkout dev
fi

cd $CDIR
