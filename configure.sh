#!/bin/sh

usage()
{
    cat <<EOF
Usage: $0 [OPTIONS]
    --help

    --bin=PATH                  - path to install binary file.
                                  default value is /usr/bin/astra
    --scripts=PATH              - path to install scripts.
                                  default value is /etc/astra/scripts

    --with-modules=PATH[:PATH]  - list of modules (by default: *)
                                  * - include all modules from ./modules dir.
                                  For example, to append custom module, use:
                                  --with-modules=*:path/to/custom/module

    --cc=GCC                    - custom C compiler (cross-compile)
    --build-static              - build static binary
    --arch=ARCH                 - CPU architecture type. (by default: native)

    --debug                     - build version for debug

    --cflags="..."              - custom compiler flags
    --ldflags="..."             - custom linker flags
EOF
    exit 0
}

SRCDIR=`dirname $0`

MAKEFILE="Makefile"
CONFFILE="config.h"

APP="astra"
APP_C="gcc"
APP_STRIP="strip"

ARG_CC=0
ARG_BPATH="/usr/bin/$APP"
ARG_SPATH="/etc/astra/scripts"
ARG_MODULES="*"
ARG_BUILD_STATIC=0
ARG_ARCH="native"
ARG_CFLAGS=""
ARG_LDFLAGS=""
ARG_DEBUG=0

set_cc()
{
    ARG_CC=1
    APP_C="$1"
    APP_STRIP=`echo $1 | sed 's/-gcc$/-strip/'`
}

while [ $# -ne 0 ] ; do
    OPT="$1"
    shift

    case "$OPT" in
        "--help")
            usage
            ;;
        "--bin="*)
            ARG_BPATH=`echo $OPT | sed -e 's/^[a-z-]*=//'`
            ;;
        "--scripts="*)
            ARG_SPATH=`echo $OPT | sed -e 's/^[a-z-]*=//' -e 's/\/$//'`
            ;;
        "--with-modules="*)
            ARG_MODULES=`echo $OPT | sed -e 's/^[a-z-]*=//'`
            ;;
        "--cc="*)
            set_cc `echo $OPT | sed 's/^--cc=//'`
            ;;
        "--build-static")
            ARG_BUILD_STATIC=1
            ;;
        "--arch="*)
            ARG_ARCH=`echo $OPT | sed -e 's/^[a-z-]*=//'`
            ;;
        "--cflags="*)
            ARG_CFLAGS=`echo $OPT | sed -e 's/^[a-z-]*=//'`
            ;;
        "--ldflags="*)
            ARG_LDFLAGS=`echo $OPT | sed -e 's/^[a-z-]*=//'`
            ;;
        "--debug")
            ARG_DEBUG=1
            ;;
        "CFLAGS="*)
            ARG_CFLAGS=`echo $OPT | sed -e 's/^[A-Z]*=//'`
            ;;
        "LDFLAGS="*)
            ARG_LDFLAGS=`echo $OPT | sed -e 's/^[A-Z]*=//'`
            ;;
        *)
            echo "Unknown option: $OPT"
            echo "For more information see: $0 --help"
            exit 1
            ;;
    esac
done

if ! which $APP_C >/dev/null ; then
    echo "C Compiler is not found :$APP_C"
    exit 1
fi

if test -f $MAKEFILE ; then
    echo "Cleaning previous build..." >&2
    make clean
    echo >&2
fi

CFLAGS_DEBUG="-O2"
if [ $ARG_DEBUG -ne 0 ] ; then
    CFLAGS_DEBUG="-g -O0"
    APP_STRIP=":"
fi

CFLAGS="$CFLAGS_DEBUG -I$SRCDIR -Wall -Wextra -pedantic \
-fno-builtin -funit-at-a-time -ffast-math"

cpucheck_c()
{
    cat <<EOF
#include <stdio.h>
int main()
{
#if defined(__i386__) || defined(__x86_64__)
    unsigned int eax, ebx, ecx, edx;
    __asm__ __volatile__ (  "cpuid"
                          : "=a" (eax)
                          , "=b" (ebx)
                          , "=c" (ecx)
                          , "=d" (edx)
                          : "a"  (1));

    if(ecx & (0x00080000 /* 4.1 */ | 0x00100000 /* 4.2 */ )) printf("-msse -msse2 -msse4");
    else if(ecx & 0x00000001) printf("-msse -msse2");
    else if(edx & 0x04000000) printf("-msse -msse2");
    else if(edx & 0x02000000) printf("-msse");
    else if(edx & 0x00800000) printf("-mmmx");
#endif
    return 0;
}
EOF
}

cpucheck()
{
    CPUCHECK="$SRCDIR/$RANDOM.cpucheck"
    cpucheck_c | $APP_C -Werror $CFLAGS -o $CPUCHECK -x c - >/dev/null 2>&1
    if [ $? -eq 0 ] ; then
        $CPUCHECK
        rm $CPUCHECK
    fi
}

if [ $ARG_CC -eq 0 ]; then
    CPUFLAGS=`cpucheck`
    if [ -n "$CPUFLAGS" ] ; then
        $APP_C $CFLAGS $CPUFLAGS -E -x c /dev/null >/dev/null 2>&1
        if [ $? -eq 0 ] ; then
            CFLAGS="$CFLAGS $CPUFLAGS"
        fi
    fi
fi

$APP_C $CFLAGS -march=$ARG_ARCH -E -x c /dev/null >/dev/null 2>&1
if [ $? -eq 0 ] ; then
    CFLAGS="$CFLAGS -march=$ARG_ARCH"
else
    echo "Error: gcc does not support -march=$ARG_ARCH" >&2
fi

CCSYSTEM=`$APP_C -dumpmachine`
case "$CCSYSTEM" in
*"linux"*)
    OS="linux"
    CFLAGS="$CFLAGS -fPIC -pthread"
    if $APP_C $CFLAGS -dM -E -xc /dev/null | grep -q "__i386__" ; then
        CFLAGS="$CFLAGS -D_FILE_OFFSET_BITS=64"
    fi
    LDFLAGS="-ldl -lm -lpthread"
    ;;
*"freebsd"*)
    OS="freebsd"
    CFLAGS="$CFLAGS -fPIC -pthread"
    LDFLAGS="-lm -lpthread"
    ;;
*"darwin"*)
    OS="darwin"
    CFLAGS="$CFLAGS -fPIC -pthread"
    LDFLAGS=""
    ;;
*"mingw"*)
    APP="$APP.exe"
    OS="mingw"
    WS32=`$APP_C -print-file-name=libws2_32.a`
    LDFLAGS="$WS32"
    ;;
*)
    echo "Unknown OS type \"$CCSYSTEM\""
    exit 1
    ;;
esac

if [ -n "$ARG_CFLAGS" ] ; then
    CFLAGS="$CFLAGS $ARG_CFLAGS"
fi

if [ $ARG_BUILD_STATIC -eq 1 ] ; then
    LDFLAGS="$LDFLAGS -static"
fi

if [ -n "$ARG_LDFLAGS" ] ; then
    LDFLAGS="$LDFLAGS $ARG_LDFLAGS"
fi

# libdvbcsa

LIBDVBCSA=0

libdvbcsa_test_c()
{
    cat <<EOF
#include <stdio.h>
#include <dvbcsa/dvbcsa.h>
int main(void) {
    struct dvbcsa_key_s *key = dvbcsa_key_alloc();
    dvbcsa_key_free(key);
    return 0;
}
EOF
}

check_libdvbcsa()
{
    O="check_libdvbcsa.o"
    R=1
    libdvbcsa_test_c | $APP_C -Werror $1 -c -o $O -x c - >/dev/null 2>&1
    if [ $? -eq 0 ] ; then
        $APP_C $O -o /dev/null $2 >/dev/null 2>&1
        R=$?
        rm $O
    fi
    return $R
}

check_libdvbcsa_all()
{
    LIBDVBCSA_LDFLAGS="-ldvbcsa"
    if check_libdvbcsa "" "$LIBDVBCSA_LDFLAGS" ; then
        LIBDVBCSA=1
        LDFLAGS="$LDFLAGS $LIBDVBCSA_LDFLAGS"
        return 0
    fi

    LIBDVBCSA_CFLAGS="-I$SRCDIR/contrib/build/libdvbcsa/src"
    LIBDVBCSA_LDFLAGS="$SRCDIR/contrib/build/libdvbcsa/libdvbcsa.a"
    if check_libdvbcsa "$LIBDVBCSA_CFLAGS" "$LIBDVBCSA_LDFLAGS" ; then
        LIBDVBCSA=1
        CFLAGS="$CFLAGS $LIBDVBCSA_CFLAGS"
        LDFLAGS="$LDFLAGS $LIBDVBCSA_LDFLAGS"
        return 0
    fi

    return 1
}

check_libdvbcsa_all

# APP flags

APP_CFLAGS="$CFLAGS -Wstrict-prototypes -std=iso9899:1999 -D_GNU_SOURCE"
APP_LDFLAGS="$LDFLAGS"

# temporary file

TMP_MODULE_MK="/tmp"
if [ ! -d "/tmp" ] ; then
    TMP_MODULE_MK="."
fi
TMP_MODULE_MK="$TMP_MODULE_MK/$APP_module.mk-$RANDOM"
touch $TMP_MODULE_MK 2>/dev/null
if [ $? -ne 0 ] ; then
    echo "ERROR: failed to build tmp file ($TMP_MODULE_MK)"
    exit 1
fi
rm -f $TMP_MODULE_MK

#

cat >&2 <<EOF
Compiler Flags:
  TARGET: $CCSYSTEM
      CC: $APP_C
  CFLAGS: $APP_CFLAGS

EOF

# makefile

rm -f $MAKEFILE
exec 5>$MAKEFILE

cat >&5 <<EOF
# generated by configure.sh

MAKEFLAGS = -rR --no-print-directory

APP         = $APP
CC          = $APP_C
CFLAGS      = $APP_CFLAGS
OS          = $OS

CORE_OBJS   =
MODS_OBJS   =

.PHONY: all clean distclean
all: \$(APP)

clean: \$(APP)-clean
	@rm -f Makefile config.h

distclean: clean
EOF

echo "Check modules:" >&2

# main app

APP_SOURCE="$SRCDIR/main.c"
APP_OBJS=""
APP_SCRIPTS=""

__check_main_app()
{
    O="main.o"
    APP_OBJS="$APP_OBJS $O"
    $APP_C $APP_CFLAGS -MT $O -MM $APP_SOURCE 2>$TMP_MODULE_MK
    if [ $? -ne 0 ] ; then
        return 1
    fi
    cat <<EOF
	@echo "   CC: \$@"
	@\$(CC) \$(CFLAGS) -DASC_SPATH=\"$ARG_SPATH\" -o \$@ -c \$<
EOF

    return 0
}

touch $CONFFILE
__check_main_app >&5
if [ $? -ne 0 ] ; then
    echo "  ERROR: $APP_SOURCE" >&2
    if [ -f $TMP_MODULE_MK ] ; then
        cat $TMP_MODULE_MK >&2
        rm -f $TMP_MODULE_MK
    fi
    exec 5>&-
    rm -f $MAKEFILE
    exec 6>&-
    rm -f $CONFFILE
    exit 1
else
    echo "     OK: $APP_SOURCE"
fi
echo "" >&5

#

select_modules()
{
    echo "$ARG_MODULES" | tr ':' '\n' | while read M ; do
        if [ -z "$M" ] ; then
            :
        elif [ "$M" = "*" ] ; then
            ls -d $SRCDIR/modules/* | while read M ; do
                if [ -f "$M/module.mk" ] ; then
                    echo "$M"
                fi
            done
        else
            echo "$M" | sed 's/\/$//'
        fi
    done
}

APP_MODULES_LIST=`select_modules`

# modules checking

APP_MODULES_CONF=""
APP_MODULES_A=""

__check_module()
{
    MODULE="$1"
    OGROUP="$2"

    SOURCES=""
    MODULES=""
    CFLAGS=""
    LDFLAGS=""
    SCRIPTS=""
    ERROR=""

    OBJECTS=""

    . $MODULE/module.mk

    if [ -n "$ERROR" ] ; then
        echo "$MODULE: error: $ERROR" >$TMP_MODULE_MK
        return 1
    fi

    if [ -n "$LDFLAGS" ] ; then
        APP_LDFLAGS="$APP_LDFLAGS $LDFLAGS"
    fi

    if [ -z "$SOURCES" ] ; then
        if [ -f "$MODULE/module.a" ] ; then
            APP_MODULES_A="$APP_MODULES_A $MODULE/module.a"
            if [ -n "MODULES" ] ; then
                APP_MODULES_CONF="$APP_MODULES_CONF $MODULES"
            fi
            return 0
        fi
        echo "$MODULE: SOURCES is not defined" >$TMP_MODULE_MK
        return 1
    fi

    echo "${MODULE}_CFLAGS = $CFLAGS"
    echo ""

    for S in $SOURCES ; do
        O=`echo $S | sed -e 's/.c$/.o/'`
        OBJECTS="$OBJECTS $MODULE/$O"
        $APP_C $APP_CFLAGS $CFLAGS -MT $MODULE/$O -MM $MODULE/$S 2>$TMP_MODULE_MK
        if [ $? -ne 0 ] ; then
            return 1
        fi
        cat <<EOF
	@echo "   CC: \$@"
	@\$(CC) \$(CFLAGS) \$(${MODULE}_CFLAGS) -o \$@ -c \$<
EOF
    done

    for S in $SCRIPTS ; do
        APP_SCRIPTS="$APP_SCRIPTS $MODULE/$S"
    done

    cat <<EOF

${MODULE}_OBJECTS = $OBJECTS
${OGROUP} += \$(${MODULE}_OBJECTS)

EOF

    if [ -n "MODULES" ] ; then
        APP_MODULES_CONF="$APP_MODULES_CONF $MODULES"
    fi

    return 0
}

check_module()
{
    MODULE="$1"
    OGROUP="$2"

    __check_module $MODULE $OGROUP >&5
    if [ $? -eq 0 ] ; then
        echo "     OK: $MODULE" >&2
    else
        echo "   SKIP: $MODULE" >&2
    fi
    if [ -f $TMP_MODULE_MK ] ; then
        cat $TMP_MODULE_MK >&2
        rm -f $TMP_MODULE_MK
    fi
}

# CORE

check_module $SRCDIR/core "CORE_OBJS"
check_module $SRCDIR/lua "CORE_OBJS"

# MODULES

for M in $APP_MODULES_LIST ; do
    check_module $M "MODS_OBJS"
done

# config.h

rm -f $CONFFILE
exec 6>$CONFFILE

cat >&6 <<EOF
/* generated by configure.sh */
#ifndef _CONFIG_H_
#define _CONFIG_H_

EOF

for M in $APP_MODULES_CONF ; do
    echo "extern int luaopen_$M(lua_State *);" >&6
done

cat >&6 <<EOF

int (*astra_mods[])(lua_State *) =
{
EOF

for M in $APP_MODULES_CONF ; do
    echo "    luaopen_$M," >&6
done

cat >&6 <<EOF
    NULL
};

#endif /* _CONFIG_H_ */
EOF

exec 6>&-

# MAKEFILE LINKER

VERSION_MAJOR=`sed -n 's/.*ASTRA_VERSION_MAJOR \([0-9]*\).*/\1/p' version.h`
VERSION_MINOR=`sed -n 's/.*ASTRA_VERSION_MINOR \([0-9]*\).*/\1/p' version.h`
VERSION="$VERSION_MAJOR.$VERSION_MINOR"

cat >&2 <<EOF

Linker Flags:
 VERSION: $VERSION
     OUT: $APP
 LDFLAGS: $APP_LDFLAGS

Install Path:
  BINARY: $ARG_BPATH
 SCRIPTS: $ARG_SPATH
EOF

cat >&5 <<EOF
LD          = $APP_C
LDFLAGS     = $APP_LDFLAGS
STRIP       = $APP_STRIP
VERSION     = $VERSION
BPATH       = $ARG_BPATH
SPATH       = $ARG_SPATH

\$(APP): $APP_OBJS \$(CORE_OBJS) \$(MODS_OBJS)
	@echo "BUILD: \$@"
	@\$(LD) \$^$APP_MODULES_A -o \$@ \$(LDFLAGS)
	@\$(STRIP) \$@

install: \$(APP)
	@echo "INSTALL: \$(BPATH)"
	@rm -f \$(BPATH)
	@cp \$(APP) \$(BPATH)
	@mkdir -p \$(SPATH)
EOF

for S in $APP_SCRIPTS ; do
    SCRIPT_NAME=`basename $S`
    cat >&5 <<EOF
	@echo "INSTALL: \$(SPATH)/$SCRIPT_NAME"
	@cp $S \$(SPATH)/$SCRIPT_NAME
EOF
done

cat >&5 <<EOF
	@echo "INSTALL: \$(SPATH)/analyze.lua"
	@cp $SRCDIR/scripts/analyze.lua \$(SPATH)/analyze.lua
	@chmod +x \$(SPATH)/analyze.lua
	@echo "INSTALL: \$(SPATH)/dvbls.lua"
	@cp $SRCDIR/scripts/dvbls.lua \$(SPATH)/dvbls.lua
	@chmod +x \$(SPATH)/dvbls.lua
	@echo "INSTALL: \$(SPATH)/xproxy.lua"
	@cp $SRCDIR/scripts/xproxy.lua \$(SPATH)/xproxy.lua
	@chmod +x \$(SPATH)/xproxy.lua

uninstall:
	@echo "UNINSTALL: \$(APP)"
	@rm -f \$(BPATH)
	@rm -rf \$(SPATH)

\$(APP)-clean:
	@echo "CLEAN: \$(APP)"
	@rm -f \$(APP) $APP_OBJS
	@rm -f \$(MODS_OBJS)
	@rm -f \$(CORE_OBJS)
EOF

exec 5>&-
