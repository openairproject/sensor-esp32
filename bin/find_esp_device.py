#!/usr/bin/python

import sys
import glob
import subprocess
import os
import re

#
# allows mapping connected ESP32 devices based on their chipId.
# Using chipId is necessary since all devboards report the same serial num and other
# attrbbutes that are usually used to map serial devices to persistent names
#
# Usage:
#   
#   find_esp_devices.py '/dev/ttyUSB?' '0xac240ac4020c' '$HOME/dev1' '0xd30aea4014e' '$HOME/dev0'
#
# 1st argument is a mask to look for connected devices,
# then each pair of arguments defines a mapping between chipId => symlink to create 
#
# chipId must be in the exact format as returned by esptool.
# You can display it using
#
#   esptool.py --port '/dev/ttyUSB0' chip_id
#

chipToSymlink={}
for i in xrange(2, len(sys.argv), 2):
  chipToSymlink[sys.argv[i].strip()]=os.path.expandvars(sys.argv[i+1].strip())

print 'look for devices \"'+sys.argv[1]+'\" and try to map links ',
print chipToSymlink

devices = glob.glob(sys.argv[1])

esptool = os.path.expandvars('$IDF_PATH')+'/components/esptool_py/esptool/esptool.py'
print 'esp tool path: '+esptool

chipToTTY={}

for device in devices:
  print 'checking '+device
  proc = subprocess.Popen([esptool,'--port',device,'chip_id'],stdout=subprocess.PIPE)
  while True:
    line = proc.stdout.readline()
    if line == '':
      break
    matcher = re.match('Chip ID: (0x.*)', line, re.M|re.I)
    if matcher:
      print 'chip:'+matcher.group(1)
      chipToTTY[matcher.group(1)]=device
      break

proc.wait()
if proc.returncode != 0:
  print 'exit due to problem with esptool ('+proc.returncode+')'
  sys.exit(proc.returncode)

print 'found following devices ',
print chipToTTY

for chipId in chipToSymlink:
  link = chipToSymlink[chipId]
  if os.path.islink(link):
    os.unlink(link)
  try:
    tty = chipToTTY[chipId]
    print 'create link '+link+'=>'+tty
    os.symlink(tty, link)
  except KeyError, e:
    pass

print 'done'