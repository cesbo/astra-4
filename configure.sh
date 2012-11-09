#!/bin/sh

usage()
{
    cat <<EOF
Usage: $0 [OPTIONS]
    --help
    --debug                     - build version for debug
    --cc=GCC                    - custom C compiler (cross-compile)
    --with-modules=PATH[:PATH]  - build with a listed modules
    --build-static              - build static binary
    --with-main-app=FILE        - build custom app
    --without-modules           - build astra core, without modules
    --without-lua               - build astra core, without lua
    CFLAGS="..."                - custom compiler flags
    LDFLAGS="..."               - custom linker flags
    LIBS="..."                  - static linked libraries
EOF
    exit 0
}

CDIR=`pwd`
cd `dirname $0`
SRCDIR=`pwd`
cd $CDIR

ARGS="$*"
ARG_DEBUG=0
ARG_CC=""
ARG_MODULES=""
ARG_BUILD_STATIC=0
ARG_MAIN_APP=""
ARG_WITHOUT_LUA=0
ARG_CFLAGS=""
ARG_LDFLAGS=""
ARG_LIBS=""

IN_CC="gcc"
IN_LD="gcc"
IN_STRIP="strip"

set_cc()
{
    ARG_CC="$1"
    IN_CC="$1"
    IN_LD="$1"
    IN_STRIP=`echo $1 | sed 's/-gcc$/-strip/'`
}

while [ $# -ne 0 ] ; do
    OPT="$1"
    shift

    case "$OPT" in
"--help") usage ;;
"--debug") ARG_DEBUG=1 ;;
"--cc="*) set_cc `echo $OPT | sed 's/^--cc=//'` ;;
"--with-modules="*) ARG_MODULES=`echo $OPT | sed -e 's/^[a-z-]*=//' -e 's/\/*:/ /g' -e 's/\/$//'` ;;
"--build-static") ARG_BUILD_STATIC=1 ;;
"--with-main-app="*) ARG_MAIN_APP=`echo $OPT | sed -e 's/^[a-z-]*=//'` ;;
"--without-modules") ARG_MODULES="_" ;;
"--without-lua") ARG_WITHOUT_LUA=1 ;;
"CFLAGS="*) ARG_CFLAGS=`echo $OPT | sed -e 's/^[A-Z]*=//'` ;;
"LDFLAGS="*) ARG_LDFLAGS=`echo $OPT | sed -e 's/^[A-Z]*=//'` ;;
"LIBS="*) ARG_LIBS=`echo $OPT | sed -e 's/^[A-Z]*=//'` ;;
*) echo "Unknown option: $OPT"; usage ;;
    esac
done

if ! which $IN_CC >/dev/null ; then
    echo "C Compiler not found :$IN_CC"
    exit 1
fi

IN_CFLAGS=""

if [ -z "$ARG_CC" ]; then
   CHECK_CPU_APP="$SRCDIR/cpucheck"
   $IN_CC -o $CHECK_CPU_APP cpucheck.c
   if [ $? -eq 0 ]; then
       IN_CFLAGS=`$CHECK_CPU_APP`
       rm $CHECK_CPU_APP
   else
       echo "Warning: CPU flags check error"
   fi
fi

if [ -z "$IN_CFLAGS" ]; then
    if $IN_CC -march=native -x c -E /dev/null >/dev/null 2>&1 ; then
        IN_CFLAGS="-march=native"
    fi
fi

if [ $ARG_DEBUG -eq 1 ]; then
    IN_CFLAGS="$IN_CFLAGS -O2 -g -DDEBUG=1 -pedantic"
    IN_STRIP=":"
else
    IN_CFLAGS="$IN_CFLAGS -O3 -fexpensive-optimizations"
fi

if [ $ARG_WITHOUT_LUA -eq 1 ]; then
    IN_CFLAGS="$IN_CFLAGS -DWITHOUT_LUA"
fi

if [ "$ARG_MODULES" = "_" ]; then
    IN_CFLAGS="$IN_CFLAGS -DWITHOUT_MODULES"
fi

IN_THREAD_MODEL=`LC_ALL='en' $IN_CC -E -v -x c /dev/null 2>&1 | grep 'Thread model:' | cut -d ' ' -f 3`

IN_TARGET_GCC=`$IN_CC -dumpmachine`
IN_TARGET=""
case "$IN_TARGET_GCC" in
    *"linux"*)
        IN_TARGET="linux"
        IN_CFLAGS="$IN_CFLAGS -fPIC"
        IN_APP_EXTENSION=""
        IN_LIB_EXTENSION=".so"
        ;;
    *"freebsd"*)
        IN_TARGET="freebsd"
        IN_CFLAGS="$IN_CFLAGS -fPIC"
        IN_LDFLAGS="-lm"
        IN_APP_EXTENSION=""
        IN_LIB_EXTENSION=".so"
        ;;
    *"darwin"*)
        IN_TARGET="darwin"
        IN_CFLAGS="$IN_CFLAGS -fPIC"
        IN_APP_EXTENSION=""
        IN_LIB_EXTENSION=".dylib"
        ;;
    *"mingw"*)
        IN_TARGET="mingw"
        IN_APP_EXTENSION=".exe"
        IN_LIB_EXTENSION=".dll"
        ;;
    *)
        echo "ERROR: failed to detect target [$IN_TARGET_GCC]"
        exit 1
        ;;
esac

IN_OUT="astra$IN_APP_EXTENSION"

IN_CFLAGS="$IN_CFLAGS \
-Wall -Wstrict-prototypes -std=gnu99 \
-fno-builtin -funit-at-a-time -ffast-math -fforce-addr \
-I$SRCDIR -I$SRCDIR/core -I$SRCDIR/lua $ARG_CFLAGS"

echo "Check modules:"

IN_LDFLAGS="$IN_LDFLAGS $ARG_LDFLAGS"
IN_LIBS="$ARG_LIBS"
IN_OBJECTS=""

case "$IN_THREAD_MODEL" in
    "posix")
        IN_CFLAGS="$IN_CFLAGS -pthread"
        IN_LDFLAGS="$IN_LDFLAGS -lpthread"
        ;;
    "win32")
        ;;
    *)
        echo "ERROR: unknown thread model $IN_THREAD_MODEL"
        ;;
esac

:>config.h
exec 5>$SRCDIR/__modules.mk
exec 6>$SRCDIR/__config_1.h
exec 7>$SRCDIR/__config_2.h

check_module() {
    MODDIR="$1"
    SOURCES=""
    MODULES=""
    CFLAGS=""
    LDFLAGS=""
    LIBS=""
    IS_ERROR=0
    if [ -d "$MODDIR" -a -f "$MODDIR/module.mk" ] ; then
        . $MODDIR/module.mk
        if [ $IS_ERROR -ne 0 ] ; then
            echo "    ERR: $MODDIR"
        elif [ -n "$SOURCES" ] ; then
            IN_IS_ERR=0
            IN_OBJECTS_TMP=""
            exec 8>$MODDIR/__module.mk
            for S in $SOURCES ; do
                O=`echo $S | sed 's/.c$/.o/'`

                IN_OBJECTS_TMP="$IN_OBJECTS_TMP $MODDIR/$O"
                $IN_CC $IN_CFLAGS $CFLAGS -MT $MODDIR/$O -MM $MODDIR/$S >&8
                [ $? -ne 0 ] && IN_IS_ERR=1 && break
                cat >&8 <<EOF
	@echo Compiling: $MODDIR/$S
	@\$(CC) \$(CFLAGS) $CFLAGS -c $MODDIR/$S -o \$@

EOF
            done
            exec 8>&-
            if [ $IN_IS_ERR -eq 0 ] ; then
                echo "     OK: $MODDIR"
                IN_OBJECTS="$IN_OBJECTS $IN_OBJECTS_TMP"
                cat $MODDIR/__module.mk >&5
                if [ -n "$MODULES" ] ; then
                    for M in $MODULES ; do
                        echo "extern int luaopen_$M(lua_State *);" >&6
                        echo "    luaopen_$M," >&7
                    done
                fi
                if [ -n "$LDFLAGS" ] ; then
                    IN_LDFLAGS="$IN_LDFLAGS $LDFLAGS"
                fi
                if [ -n "$LIBS" ] ; then
                    IN_LIBS="$IN_LIBS $LIBS"
                fi
            else
                echo "    ERR: $MODDIR"
            fi
            rm $MODDIR/__module.mk
        fi
    fi
}

check_module $SRCDIR/core
IN_OBJECTS_CORE=$IN_OBJECTS
IN_OBJECTS=""

IN_OBJECTS_LUA=""
if [ $ARG_WITHOUT_LUA -ne 1 ] ; then
    check_module $SRCDIR/lua
    IN_OBJECTS_LUA=$IN_OBJECTS
    IN_OBJECTS=""
fi

IN_MAIN_APP=""
if [ -z "$ARG_MAIN_APP" ] ; then
    IN_MAIN_APP="$SRCDIR/main.c"
else
    IN_MAIN_APP="$ARG_MAIN_APP"
fi

IN_OBJECTS=`echo $IN_MAIN_APP | sed 's/.c$/.o/'`
CC_RET=`$IN_CC $IN_CFLAGS -MT $IN_OBJECTS -MM $IN_MAIN_APP`
if [ $? -eq 0 ] ; then
    echo "     OK: $IN_MAIN_APP"
    cat >&5 <<EOF
${CC_RET}
	@echo Compiling: $IN_MAIN_APP
	@\$(CC) \$(CFLAGS) -c $IN_MAIN_APP -o \$@

EOF
else
    echo "    ERR: $IN_MAIN_APP"
fi

if [ -z "$ARG_MODULES" ] ; then
    ARG_MODULES=`ls -d $SRCDIR/modules/*`
fi

if [ "$ARG_MODULES" != "_" ] ; then
    for M in $ARG_MODULES ; do
        check_module "$M"
    done
fi

exec 5>&-
exec 6>&-
exec 7>&-

{
    echo "/* generated by configure.sh */"
    echo "#ifndef _CONFIG_H_"
    echo "#define _CONFIG_H_"
    cat $SRCDIR/__config_1.h
    echo "int (*astra_mods[])(lua_State *) ="
    echo "{"
    cat $SRCDIR/__config_2.h
    echo "    NULL"
    echo "};"
    echo "#endif /* _CONFIG_H_ */"
} >$SRCDIR/config.h
rm -f $SRCDIR/__config_1.h $SRCDIR/__config_2.h

cat <<EOF

Build flags:
    OUT: $IN_OUT
 TARGET: $IN_TARGET_GCC
     CC: $IN_CC
 CFLAGS: $IN_CFLAGS
LDFLAGS: $IN_LDFLAGS
   LIBS: $IN_LIBS
EOF

{
cat <<EOF
# generated by configure.sh
# $ARGS

MAKEFLAGS = -rR --no-print-directory

CC = $IN_CC
LD = $IN_LD
STRIP = $IN_STRIP

CFLAGS = $IN_CFLAGS
LDFLAGS = $IN_LDFLAGS
LIBS = $IN_LIBS

OBJS_CORE = $IN_OBJECTS_CORE
OBJS_LUA = $IN_OBJECTS_LUA
OBJS_MODULES = $IN_OBJECTS
OBJS = \$(OBJS_CORE) \$(OBJS_LUA) \$(OBJS_MODULES)

.PHONY: all clean distclean

all: $IN_OUT

EOF
cat $SRCDIR/__modules.mk

cat <<EOF

$IN_OUT: \$(OBJS)
	@echo Link: \$@
	@\$(LD) \$(OBJS) \$(LIBS) -o \$@ \$(LDFLAGS)
	@\$(STRIP) \$@

install: $IN_OUT
	@echo Install:
	@rm -f /usr/bin/\$<
	@cp -v \$< /usr/bin/
	@mkdir -p /etc/astra/helpers
	@cp -v $SRCDIR/helpers/base.lua /etc/astra/helpers/
	@cp -v $SRCDIR/helpers/stream.lua /etc/astra/helpers/
	@cp -v $SRCDIR/helpers/json.lua /etc/astra/helpers/
	@cp -v $SRCDIR/helpers/event.lua /etc/astra/helpers/
	@cp -v $SRCDIR/helpers/camgroup.lua /etc/astra/helpers/
	@cp -v $SRCDIR/helpers/output_http.lua /etc/astra/helpers/

uninstall:
	@echo Uninstall:
	@rm -v /usr/bin/$IN_OUT
	@rm -v /etc/astra/helpers/base.lua
	@rm -v /etc/astra/helpers/stream.lua
	@rm -v /etc/astra/helpers/json.lua
	@rm -v /etc/astra/helpers/event.lua
	@rm -v /etc/astra/helpers/camgroup.lua
	@rm -v /etc/astra/helpers/output_http.lua
	@ls /etc/astra/helpers/* 1>/dev/null 2>&1 || rm -rfv /etc/astra/helpers
	@ls /etc/astra/* 1>/dev/null 2>&1 || rm -rfv /etc/astra && echo "Please, check /etc/astra and remove manualy"

clean:
	@echo Clean
	@rm -vf \$(OBJS_CORE) \$(OBJS_MODULES) $IN_OUT

distclean: clean
	@rm -vf \$(OBJS_LUA) Makefile config.h
	@if test -d contrib/build ; then echo "contrib/build" && rm -rf contrib/build ; fi
EOF
} >$SRCDIR/Makefile
rm $SRCDIR/__modules.mk
