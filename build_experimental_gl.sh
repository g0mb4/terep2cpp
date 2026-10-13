#!/bin/bash

pushd host/renderizeitor
    make
popd

make sdl GLR=1 GLR_DIR=host GLR_LIB=host/renderizeitor/librenderizeitor_static.a -j 16