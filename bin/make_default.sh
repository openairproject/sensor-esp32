#!/bin/sh

export BATCH_BUILD=1
make defconfig all flash $1 -j5