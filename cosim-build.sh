#!/bin/bash
if [ "$COSIM_PATH" = "" ] ; then
  COSIM_PATH="/home/praneeth/git/simbricks"
fi

export CPPFLAGS="-I$COSIM_PATH/lib"
export LDFLAGS="-L$COSIM_PATH/lib/simbricks/nicif/ -lnicif -L$COSIM_PATH/lib/simbricks/network/ -lnetwork -L$COSIM_PATH/lib/simbricks/base/ -lbase"
export CXXFLAGS="-Wno-range-loop-construct"
if [ "$1" = "configure" ] ; then
  ./waf configure --enable-examples --enable-mpi
fi
./waf build --enable-examples --enable-mpi
