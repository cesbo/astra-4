#!/bin/bash

DEV_BRANCH="astra-4"

usage()
{
    cat <<EOF
Usage: ./commit.sh OPTIONS

Options:
    --help
        This message

    --feature MESSAGE [FILES]
        Commit to feature branch. Version is not changed
    --hotfix MESSAGE [FILES]
        Commit to '$DEV_BRANCH'. Dev version up

    --major
        Merge '$DEV_BRANCH' branch to 'master'
    --minor
        Merge '$DEV_BRANCH' branch to 'master'
    --dev
        Merge current branch to '$DEV_BRANCH'. Dev version up

Workflow:
    1. Feature
        # Start new branch
        git checkout -b AS-X
        # Commit changes into feature branch
        ./commit.sh --feature 'AS-X changes desciption' [files]
        # Merge to dev
        ./commit.sh --dev
        # Remove feature branch
        git branch -d AS-X
        # Remove remote branch if needed
        git push origin :AS-X
    2. Hotfix
        # Commit changes into dev-branch directly
        ./commit.sh --hotfix 'AS-X changes desciption' [files]
    3. Release
        # After 'Feature' or after 'Hotfix'
        ./commit.sh --minor
EOF
    exit 1
}

[ $# -lt 1 ] && usage
echo "$1" | grep -vq "^--" && usage
[ "$1" = "--help" -o "$1" = "-h" ] && usage

if [ ! -f "commit.sh" ] ; then
    echo "Error: project is not found in the current direcory"
    exit 2
fi

ASTRA_VERSION_MAJOR=`cat version.h | grep "ASTRA_VERSION_MAJOR " | cut -d ' ' -f 3`
ASTRA_VERSION_MINOR=`cat version.h | grep "ASTRA_VERSION_MINOR " | cut -d ' ' -f 3`
ASTRA_VERSION_DEV=`cat version.h | grep "ASTRA_VERSION_DEV " | cut -d ' ' -f 3`

BRANCH=`git branch | grep '^\*' | cut -d ' ' -f 2`

check_branch()
{
    if [ "$BRANCH" != "$DEV_BRANCH" ] ; then
        echo "$DEV_BRANCH is required."
        echo "Current branch: $BRANCH"
        exit 2
    fi
}

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

check_changes()
{
    if ! git status | grep -q '^nothing' ; then
        echo "Changes is not staged or not commited"
        echo "Make commit or save it to stash"
        exit 3
    fi
}

release_version()
{
    check_branch
    check_changes

    ASTRA_VERSION_DEV=0
    version_up
    git commit -m "version up" version.h

    VERSION="v.$ASTRA_VERSION_MAJOR.$ASTRA_VERSION_MINOR"
    git checkout master
    git merge --no-ff $DEV_BRANCH -m "$VERSION merge with dev"
    git tag "$VERSION"

    git checkout $DEV_BRANCH
}

case "$1" in
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
    "--hotfix")
        let ASTRA_VERSION_DEV=ASTRA_VERSION_DEV+1
        version_up

        shift
        MSG="$1"
        shift

        if [ $# -ne 0 ] ; then
            git commit -m "$MSG" $* version.h
        else
            git commit -am "$MSG"
        fi
        ;;
    "--major")
        let ASTRA_VERSION_MAJOR=ASTRA_VERSION_MAJOR+1
        ASTRA_VERSION_MINOR=0
        release_version
        ;;
    "--minor")
        let ASTRA_VERSION_MINOR=ASTRA_VERSION_MINOR+1
        release_version
        ;;
    "--dev")
        check_changes

        git checkout $DEV_BRANCH
        git merge --no-ff --no-commit $BRANCH
        [ $? -ne 0 ] && exit

        git reset HEAD version.h
        git checkout -- version.h

        let ASTRA_VERSION_DEV=ASTRA_VERSION_DEV+1
        version_up
        git commit -am "merge with $BRANCH"
        ;;
    *)
        usage
        ;;
esac
