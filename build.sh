#!/bin/sh
# build script for POSIX systems (Linux/MacOS/FreeBSD)

gcc --version > /dev/null 2>&1 && CC="gcc"
clang --version > /dev/null 2>&1 && CC="clang"

DEFINES="-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE"
FLAGS="-std=c99 $DEFINES -Wall -Wextra -pedantic -lm"
D_FLAGS="-g -ggdb"
R_FLAGS="-DNDEBUG -O2 -s"
file="wavgen"

if [ "$1" = "debug" ]; then
    mode="DEBUG"
    args="$FLAGS $D_FLAGS"
else
    mode="RELEASE"
    args="$FLAGS $R_FLAGS"
fi

printf "\033[1;44mBuilding rffmpeg in $mode mode...\033[0m\n"

set -x
$CC -o $file $file.c $args || exit 1
