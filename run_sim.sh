#!/bin/bash

rm -rf /home/praneeth/env/*
./waf build

for (( c=0; c<$2; c++ ))
do 
./waf --run $1 --command-template="%s --systemId=$c --envDir=/home/praneeth/env/" &
sleep 2
done

trap "killall $1; echo SIGINT; exit 1" INT
wait