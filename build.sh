#!/bin/bash
#
# to build the debugging version of lpeg: ./build.sh debug
#                              otherwise: ./build.sh
if [ "$1"=="debug" ]; then
    debug_arg='COPT="-DLPEG_DEBUG -g"'
    echo COMPILING WITH DEBUG OPTION:  "${debug_arg}"
fi

pushd src

make clean
make macosx LUADIR=../../lua/include "${debug_arg}"
if [ $? -ne 0 ] ; then exit $?; fi
echo "Compile successful, now copying to ../../lib"
cp lpeg.so ../../../lib/

popd
