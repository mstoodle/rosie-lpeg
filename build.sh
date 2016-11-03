#!/bin/bash
pushd src

make clean
make macosx LUADIR=../../rosie-pattern-language/tmp/lua-5.3.2/include
if [ $? -ne 0 ] ; then exit $?; fi
echo "Compile successful, now copying to ../rosie-pattern-language/lib"
cp lpeg.so ../../rosie-pattern-language/lib

popd
