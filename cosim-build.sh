#!/bin/bash
if [ "$COSIM_PATH" = "" ] ; then
  COSIM_PATH="/home/praneeth/git/simbricks"
fi

# export CPPFLAGS="-I$COSIM_PATH/lib -std=c++11"
# export LDFLAGS="-L$COSIM_PATH/lib/simbricks/nicif/ -lnicif -L$COSIM_PATH/lib/simbricks/network/ -lnetwork -L$COSIM_PATH/lib/simbricks/base/ -lbase"
# export CXXFLAGS="-Wno-range-loop-construct"
export CC='gcc-5'
export CXX='g++-5'

if [ "$1" = "configure" ] ; then
  ./waf configure --enable-examples --enable-mpi --build-profile=debug
fi
./waf build --enable-examples --enable-mpi --build-profile=debug
