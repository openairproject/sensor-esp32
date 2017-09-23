#!/bin/sh

$IDF_PATH/components/espcoredump/espcoredump.py dbg_corefile -t b64 -c logs/core.dat build/sensor-esp32.elf