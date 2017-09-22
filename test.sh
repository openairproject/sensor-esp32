#https://www.esp32.com/viewtopic.php?t=2867

project=`pwd`
make -C unit-test-app EXTRA_COMPONENT_DIRS=$project/components TEST_COMPONENTS='oap_common ota awsiot bmx280 pmsx003' flash monitor -j5