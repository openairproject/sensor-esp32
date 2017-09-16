#https://www.esp32.com/viewtopic.php?t=2867

CONFIG_OAP_CONTROL_PANEL=1
project=`pwd`
make -C ${IDF_PATH}/tools/unit-test-app EXTRA_COMPONENT_DIRS=$project/components TEST_COMPONENTS='oap_common ota awsiot' flash monitor -j5