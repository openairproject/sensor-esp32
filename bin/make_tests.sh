#!/bin/sh

#https://www.esp32.com/viewtopic.php?t=2867

bin="$(dirname "$0")"
. bin/test_components.sh
export BATCH_BUILD=1
project=`pwd`

make -C unit-test-app EXTRA_COMPONENT_DIRS=$project/components TEST_COMPONENTS="$TEST_COMPONENTS" defconfig all flash $1 -j5