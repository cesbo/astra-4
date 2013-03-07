SOURCES_CORE="lapi.c lauxlib.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c lmem.c loadlib.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c ltm.c lundump.c lvm.c lzio.c"
SOURCES_LIBS="lbaselib.c lbitlib.c lcorolib.c ldblib.c linit.c liolib.c lmathlib.c loslib.c lstrlib.c ltablib.c "
SOURCES="$SOURCES_CORE $SOURCES_LIBS"

case "$OS" in
"linux")
    CFLAGS="-DLUA_USE_LINUX"
    LDFLAGS="-ldl -lm"
    ;;
"freebsd")
    CFLAGS="-DLUA_USE_LINUX"
    ;;
"darwin")
    CFLAGS="-DLUA_USE_MACOSX"
    ;;
"mingw")
    CFLAGS="-DLUA_BUILD_AS_DLL"
    ;;
*)
    ;;
esac

CFLAGS="-DLUA_COMPAT_ALL $CFLAGS"
