#!/bin/sh

#https://www.esp32.com/viewtopic.php?t=2867

bin="$(dirname "$0")"
. bin/test_components.sh
project=`pwd`
echo $TEST_COMPONENTS
make -C unit-test-app EXTRA_COMPONENT_DIRS=$project/components TEST_COMPONENTS="$TEST_COMPONENTS" all flash monitor -j5