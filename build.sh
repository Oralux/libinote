#!/bin/bash -e

BASE=$(dirname $(realpath "$0"))
NAME=$(basename "$0")

cd "$BASE"

usage() {
	echo "
Usage: 
 $NAME [options]

Build libinote and tests.

Options: 
-c, --clean		   clean-up: delete the build directory and object files
-d, --debug        compile with debug symbols 
-h, --help         display this help 
-m, --mach <arch>  target architecture
		   		   possible value: i686; by default: current arch
-i, --install      install dir
-t, --test         run tests

Example:
# compile libinote
 $0

# compile libinote with debug symbols
 $0 -d

# compile libinote, run tests
 $0 -t

" 

}

getVersion() {
	local value
	local i
	for i in INOTE_VERSION_MAJOR INOTE_VERSION_MINOR INOTE_VERSION_PATCH; do
		value=$(awk "/$i/{print \$3}" $BASE/src/conf.h)
		eval export $i=$value
	done
	export VERSION=$INOTE_VERSION_MAJOR.$INOTE_VERSION_MINOR.$INOTE_VERSION_PATCH
}

cleanup() {
	(cd src/libinote && make clean)
	(cd src/test && make clean)
}

unset CC CFLAGS CLEAN DBG_FLAGS HELP INSTALL ARCH STRIP TEST

OPTIONS=`getopt -o cdhi:m:t --long clean,debug,help,install:,mach:,test \
             -n "$NAME" -- "$@"`
[ $? != 0 ] && usage && exit 1
eval set -- "$OPTIONS"

while true; do
  case "$1" in
    -c|--clean) CLEAN=1; shift;;
    -d|--debug) export DBG_FLAGS="-ggdb -DDEBUG"; export STRIP=test; shift;;
    -h|--help) HELP=1; shift;;
    -i|--install) INSTALL=$2; shift 2;;
    -m|--mach) ARCH=$2; shift 2;;
    -t|--test) TEST=1; shift;;
    --) shift; break;;
    *) break;;
  esac
done

case "$ARCH" in
	x86|i386|i586|i686) ARCH=i686;;	
	*)
		ARCH=$(uname -m);;
esac

[ -n "$HELP" ] && usage && exit 0

cd "$BASE"
SRCDIR="$BASE/src"

[ -z "$INSTALL" ] && INSTALL="$BASE/build/$ARCH/usr"

export DESTDIR="$INSTALL"

cleanup
[ -n "$CLEAN" ] && exit 0

CFLAGS="$DBG_FLAGS"
unset LDFLAGS
if [ "$ARCH" = "i686" ]; then
	CFLAGS="$CFLAGS -m32"
	LDFLAGS="-m32"
fi
export CFLAGS=$CFLAGS
export LDFLAGS=$LDFLAGS
for i in src/libinote src/test; do
	( cd $i; make clean; make all; make install )
done

if [ -n "$TEST" ]; then
	cd src/test
	./inote.sh
fi
