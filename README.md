# OpenAirProject AirQuality / Dust Meter

## Features

This meter measures dust (pm1, pm2.5, pm10 particles) and, optionally, other environmental conditions like temperature, pressure and humidity.
Measurements occurs in configured intervals and the result is an average of multiple recorded samples.
It handles 'warming period' necessary by the sensor to force proper air flow. 

Current air quality is indicated with different color of RGB led (blue color means that first measurement has not completed yet).

Data is sent via local wifi to one of configured services (currently - ThingSpeak, AWS IoT is coming soon).
After booting up for the first time, sensor becomes an access point and enables user to configure wifi access via browser.

## Hardware

Required parts:

ESP32 DevKit Board (other boards should also work after minor modifications).
Plantower PMS5003 (or PMS3003 or PMS7003)
Any push button

Optional parts:

BMP280 sensor for temperature/airpressure readings
RGB Led (common kathode) + 3 resistors (330ohms+)

### Wiring

Here. I draw it myself.

![Schema](doc/images/schema.jpg?raw=true)

Assembling is rather trivial.

PMSx003 sensor requires 5V power, although it communicates with standard 3V3, so no TTL converter is required.
Connect RGB led via resistors to ESP32 and to GND.
Connect BMP280 directly to ESP32.
Connect button to one of pins to pull up when pressed.

ESP32 chip features GPIO matrix which means (theoretically) that programmatically we can change a function of any I/O pin.
That being said, on ESP32 DevKit board some pins are already designated to perform specific function and may not work
properly (or cause side effects) when assigned to other interfaces. 

Firmware is pre-configured to use following GPIO which were tested with ESP32 DevKit board.

These assignments can be changed via 'make menuconfig'.

	PMSx003 TX  => 34
	PMSx003 SET => 10

	BMx280 SDA	=> 25
	BMx280 SCL	=> 26
	
	LED R		=> 12
	LED G		=> 27
	LED B		=> 14
	
	BUTTON		=> 35
	
### Power consumption
	
Sensor requires 5V+ and consumes ~150mA up to ~170mA. During booting power consumption can be 
slightly higher (~250mA), but it is still low enough to be powered directly from any decent
USB phone charger or from PC.

Despite low voltage, if you are going to use sensor outdoor 
or in any unfriendly environment (e.g. high humidity), please
take it under consideration during build, isolate all connections properly
 and use appropriate case/enclosure (more on that soon).

## Uploading firmware

To run your sensor you first need to flash ESP32 chip with OAP firmware.

To do so, you don't need to clone this project nor compile it - you can use pre-compiled firmware and a dedicated tool to handle the upload.

The procedure below was tested on Mac and Linux (Windows users, by now you should know the drill).

1. Get proper USB cable to connect the sensor to your PC. All USB cables look similar, but some of them (which are usually thinner than others), cannot transfer data and are used only for powering/charging devices and are not suitable for our task.

2. While Linux may detect sensor (or rather - usb driver chip installed on the esp32 board), other systems will most likely require installing a custom USB driver that matches the chip. Its type depends on the ESP32 board, but in majority of cases it should be a Silabs chip for which a driver can be found at <http://www.silabs.com/products/mcu/pages/usbtouartbridgevcpdrivers.aspx>).

3. Once driver is installed and sensor is connected with usb cable to PC, do

		ls -la /dev/tty*
	
	to find out what name of the serial port was assigned to our sensor (the easiest way is to list this folder with sensor disconnected and connected and compare results). Note it down. On my Mac it was "/dev/tty.SLAB_USBtoUART", on Linux - "/dev/ttyUSB0" or /dev/ttyUSB1" but it may be different depending on the system configuration.

4. Now download the firmware  (three *.bin files) for the latest stable release that you can find here <https://github.com/openairproject/sensor-esp32/releases>.

5. Now it is time to install "esptool", which is a firmware uploader from the manufacturer of ESP32 chip (Espressif company). Detailed instructions can be found here: <https://github.com/espressif/esptool/blob/master/README.md>, but if you already have Python and pip installed, just do

		pip install esptool
	
	By default it should be installed to "/usr/local/bin/".

6. It is time to perform the flashing:

		esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 --before default_reset --after hard_reset write_flash -u --flash_mode dio --flash_freq 40m --flash_size detect 0x1000 bootloader.bin 0x30000 sensor-esp32.bin 0x8000 partitions.bin
	
	Use --port from the step 3, and three bin files downloaded in step 4.
	
	In most cases, esptool will be able to switch your sensor into 'flashing' mode automatically and reset it afterwords to make it ready to go - it takes a few seconds. Some boards however require manual operation to activate this mode (esptool connection will timeout).
	To do so, disconnect the sensor from USB, press and hold small button labeled "EN" on esp32 board (next to micro usb port), connect the sensor, run the command above and then release the button. 

## First run

After booting the sensor for the first time, sensor will switch into Access Point mode and create a wifi network

	ssid: OpenAirProject-XXXXXX
	pass: cleanair
	
After connecting to this network, open following url

	http://192.168.1.1

and configure sensor settings using web control panel (most notably - your home wifi ssid/pass).
After rebooting sensor will connect to your wifi
.Web control panel will still be available, but at IP that was specified or assigned to sensor by your router.


If there's a need to force sensor into Access Point mode again (e.g. when it can't connect to specified wifi),
reboot the device with a control button pressed down.

![Schema](doc/images/sensor_settings.png?raw=true=320x)

## Building firmware

This part is for advanced users, in most cases you should be fine with installing pre-build firmware.

OAP firmware is written in native espressif-sdk v2.1 <http://esp-idf.readthedocs.io/en/v2.1/get-started/index.html>

After installing and setting up SDK, connect your ESP32 board to your PC. See notes regarding USB cable and driver in a chapter above.

To configure and build sources
	
	make -j5
	
You can always bring up configuration menu via

	make menuconfig	
		
During the first run, a menuconfig should appear where you need to configure some parameters of your setup,
most notably - UART port.

In components submenu there's a few configuration settings related to OAP hardware setup (e.g. gpio pin assignments),
and "OAP Main" menu where you can change various functional parameters.

** ATTENTION. main task stack should be increased to 10K if you're gonna use AWSIoT (via menuconfig) **	

All settings are saved in sdkconfig file.

to flash the module and read from the serial output

	make flash monitor
	
That's it.

Happy DIY time!
	
---

![Prototype](doc/images/prototype.jpg?raw=true=600x)
