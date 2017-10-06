#!/bin/sh

export BATCH_BUILD=1
make defconfig all $1 -j5 > /dev/null