#!/usr/bin/python
import serial
import sys
import time

ser = serial.Serial(None,115200,timeout=None,rtscts=False,xonxoff=True)
ser.port=sys.argv[1]
ser.dtr=False
ser.rts=False #otherwise it will wait for download after reboot
ser.open()

try:
    while True:
      line = ser.readline()
      print line,
      #to work with 'tee'
      sys.stdout.flush()
finally:
    ser.close()    