#!/bin/sh

#https://www.esp32.com/viewtopic.php?t=2867

project=`pwd`
export BATCH_BUILD=1
make -C unit-test-app EXTRA_COMPONENT_DIRS=$project/components TEST_COMPONENTS='oap_common ota awsiot bmx280 pmsx003' defconfig all flash $1 -j5