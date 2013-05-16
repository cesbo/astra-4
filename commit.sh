#!/bin/bash

DEV_BRANCH="astra-4"

usage()
{
    cat <<EOF
Usage: ./commit.sh options [files]
Options:
    --help              This message

    --feature MESSAGE   Commit to feature branch. Version is not changing
    --hotfix MESSAGE    Commit to '$DEV_BRANCH'. Dev version up

    --major             Merge '$DEV_BRANCH' branch to 'master'
    --minor             Merge '$DEV_BRANCH' branch to 'master'
    --dev               Merge current branch to '$DEV_BRANCH'. Dev version up

Workflow:
    1. Feature
        # Start new branch
        git checout -b AS-X
        # Commit changes into feature branch
        ./commit.sh --feature 'AS-X changes desciption' [files]
        # Merge to dev
        ./commit.sh --dev
    2. Hotfix
        # Commit changes into dev-branch directly
        ./comit.sh --hotfix 'AS-X changes desciption' [files]
    3. Releas
        # After 'Feature' or after 'Hotfix'
        ./commit --minor
EOF
    exit 1
}

check_changes()
{
    if ! git status | grep -q 'nothing added to commit' ; then
        echo "Changes is not staged or not commited"
        echo "Make commit or save it to stash"
        exit 3
    fi
}

[ $# -lt 1 ] && usage
echo "$1" | grep -vq "^--" && usage
[ "$1" = "--help" -o "$1" = "-h" ] && usage

if [ ! -f "commit.sh" ] ; then
    echo "Error: project is not found in the current direcory"
    exit 2
fi

BRANCH=`git branch | grep '^\*' | cut -d ' ' -f 2`

if [ "$BRANCH" != "$DEV_BRANCH" ] ; then
    case "$1" in
        "--dev")
            check_changes
            git checkout $DEV_BRANCH
            git merge --no-ff $BRANCH --no-commit
            ./commit.sh --hotfix "$BRANCH merge"
            ;;
        "--feature")
            shift
            MSG="$1"
            shift

            if [ $# -ne 0 ] ; then
                git commit -m "$MSG" $*
            else
                git commit -am "$MSG"
            fi
            ;;
        *)
            echo "$DEV_BRANCH is required. Current branch: $BRANCH"
            echo ""
            usage
            ;;
    esac

    exit
fi

ASTRA_VERSION_MAJOR=`cat version.h | grep "ASTRA_VERSION_MAJOR " | cut -d ' ' -f 3`
ASTRA_VERSION_MINOR=`cat version.h | grep "ASTRA_VERSION_MINOR " | cut -d ' ' -f 3`
ASTRA_VERSION_DEV=`cat version.h | grep "ASTRA_VERSION_DEV " | cut -d ' ' -f 3`

version_up()
{
    cat >version.h <<EOF
#ifndef _VERSION_H_
#define _VERSION_H_ 1

#define ASTRA_VERSION_MAJOR $ASTRA_VERSION_MAJOR
#define ASTRA_VERSION_MINOR $ASTRA_VERSION_MINOR
#define ASTRA_VERSION_DEV $ASTRA_VERSION_DEV

#endif /* _VERSION_H_ */
EOF
    git add version.h
}

release_version()
{
    check_changes

    VERSION="v.$ASTRA_VERSION_MAJOR.$ASTRA_VERSION_MINOR"
    git checkout master
    git merge --no-ff $DEV_BRANCH --no-commit

    version_up

    ./commit.sh --feature "$VERSION merge"
    git tag "$VERSION"
    git checkout $DEV_BRANCH
}

case "$1" in
    "--major")
        let ASTRA_VERSION_MAJOR=ASTRA_VERSION_MAJOR+1
        ASTRA_VERSION_MINOR=0
        ASTRA_VERSION_DEV=0
        release_version
        exit
        ;;
    "--minor")
        let ASTRA_VERSION_MINOR=ASTRA_VERSION_MINOR+1
        ASTRA_VERSION_DEV=0
        release_version
        exit
        ;;
    "--hotfix")
        let ASTRA_VERSION_DEV=ASTRA_VERSION_DEV+1
        ;;
    *)
        usage
        ;;
esac

version_up

shift
MSG="$1"
shift

if [ $# -ne 0 ] ; then
    git commit -m "$MSG" $* version.h
else
    git commit -am "$MSG"
fi
