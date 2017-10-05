#!/usr/bin/python
import serial
import sys
import time

#'/dev/tty.SLAB_USBtoUART'

ser = serial.Serial(sys.argv[1],115200,timeout=1)

def readall(exp, timeout = 5):
    line = ser.readline()
    lastline = ''
    start = time.time()
    while line != exp:
        lastline = line     
        
        if line == 'Rebooting...':
            lastline = 'UNEXPECTED REBOOT'
            break
        
        if ":FAIL" in line or ":PASS" in line:
            start = time.time() 
            
        if time.time() - start > timeout: 
            lastline = 'TIMEOUT after '+str(timeout)+' seconds'
            break   
        
        line = ser.readline()
        sys.stdout.write(line)
        sys.stdout.flush()
        line = line.strip()
    return lastline
        
def wait_for_test_result():
    return readall('Enter next test, or \'enter\' to see menu',30)            

ser.write('\n');
readall('Here\'s the test menu, pick your combo:')
readall('')
ser.write('*' if len(sys.argv) <= 2 else sys.argv[2])
ser.write('\n')
result = wait_for_test_result().strip()
sys.stdout.write('TEST RESULT: '+result)
ser.close()
sys.exit(0 if result == 'OK' else 1)