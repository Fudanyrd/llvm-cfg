#!/usr/bin/env sh
set -ex

build_dir=$( dirname $CC )
flags=$( $build_dir/llvm-config --cxxflags )
ofile=$1
shname=$( which sh )

rm $ofile || touch $ofile

# printf \\\\x23\\\\x21/usr/bin/env sh\\\\x0a >> $ofile
# echo $shname -ex >> $ofile
echo "set -ex" >> $ofile
echo CC=$CC >> $ofile
echo CXX=\"$CXX\" >> $ofile
echo CXXFLAGS=\"$flags\" >> $ofile
echo $CXX $flags "../pass/cfg-edge/CfgEdgePass.cpp -g -O2 -fpic -shared -o pass/cfg-edge/cfg-edge.so" >> $ofile
echo $CXX $flags "../pass/func-entry/FuncEntryPass.cpp -g -O2 -fpic -shared -o pass/func-entry/func-entry.so" >> $ofile
echo $CXX $flags "../pass/func-call/FuncCallPass.cpp -g -O2 -fpic -shared -o pass/func-call/func-call.so" >> $ofile
echo $CXX $flags "../pass/null-malloc/NullMallocPass.cpp -g -O2 -fpic -shared -o pass/null-malloc/null-malloc.so" >> $ofile

chmod +x $ofile
exit 0
