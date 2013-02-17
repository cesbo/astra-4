#!/bin/bash

usage()
{
    echo "Usage: ./commit.sh option message [files]"
    echo "Options:"
    echo "    --major           major version"
    echo "    --minor           minor version"
    echo "    --dev             dev version"
    exit 1
}

[ $# -lt 1 ] && usage
echo "$1" | grep -vq "^--" && usage

if [ ! -f "commit.sh" ] ; then
    echo "Error: project is not found in the current direcory"
    exit 2
fi

BRANCH=`git branch | grep '^\*' | cut -d ' ' -f 2`
if [ "$BRANCH" != "dev" ] ; then
    echo "Error: checkout to the dev branch"
    exit 3
fi

ASTRA_VERSION_MAJOR=`cat version.h | grep "ASTRA_VERSION_MAJOR " | cut -d ' ' -f 3`
ASTRA_VERSION_MINOR=`cat version.h | grep "ASTRA_VERSION_MINOR " | cut -d ' ' -f 3`
ASTRA_VERSION_DEV=`cat version.h | grep "ASTRA_VERSION_DEV " | cut -d ' ' -f 3`

case "$1" in
    "--major")
        let ASTRA_VERSION_MAJOR=ASTRA_VERSION_MAJOR+1
        ASTRA_VERSION_MINOR=0
        ASTRA_VERSION_DEV=0
        ;;
    "--minor")
        let ASTRA_VERSION_MINOR=ASTRA_VERSION_MINOR+1
        ASTRA_VERSION_DEV=0
        ;;
    "--dev")
        let ASTRA_VERSION_DEV=ASTRA_VERSION_DEV+1
        ;;
    *)
        usage
        ;;
esac
shift

cat >version.h <<EOF
#ifndef _VERSION_H_
#define _VERSION_H_ 1

#define ASTRA_VERSION_MAJOR $ASTRA_VERSION_MAJOR
#define ASTRA_VERSION_MINOR $ASTRA_VERSION_MINOR
#define ASTRA_VERSION_DEV $ASTRA_VERSION_DEV

#endif /* _VERSION_H_ */
EOF

VERSION="v.$ASTRA_VERSION_MAJOR.$ASTRA_VERSION_MINOR"

MSG=$1
shift

GITRET=1
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
    exit 4
fi

if [ $ASTRA_VERSION_DEV -eq 0 ] ; then
    git checkout master
    git merge --no-ff -m "$VERSION merge" dev
    git tag "$VERSION"
    git checkout dev
fi
