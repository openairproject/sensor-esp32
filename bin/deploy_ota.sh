#!/bin/sh

#
# $1 - release version
#

INDEX_FILE=build/index.txt
DEST_FOLDER=s3://openairproject.com/ota

printf "$1|$1/sensor-esp32.bin|" > $INDEX_FILE

cat build/sensor-esp32.bin | openssl dgst -sha256 -binary | xxd -p -c 100 >> $INDEX_FILE
cat $INDEX_FILE

aws s3 cp build/sensor-esp32.bin $DEST_FOLDER/$1/sensor-esp32.bin --profile oap
aws s3 cp $INDEX_FILE $DEST_FOLDER/index.txt --profile oap