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
Plantower PMS5003 (or 3003. 7003 should work too, but it wasn't tested yet)
Any push button

Optional parts:

BMP280 sensor for temperature/airpressure readings
RGB Led (common kathode) + 3 resistors (330ohms+)

### Wiring

Here. I draw it myself.

![Schema](doc/schema.jpg?raw=true)

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

## Building firmware

Firmware was written with native espressif-sdk [https://github.com/espressif/esp-idf].
After installing and setting up SDK, connect your ESP32 board to your PC.
This may require installing custom USB driver (it depends on uart chipset used on your ESP32 board, for DevKit it 
should be Silabs chip - [http://www.silabs.com/products/mcu/pages/usbtouartbridgevcpdrivers.aspx]).

To configure and build sources
	
	make -j5
	
During the first run, a menuconfig should appear where you need to configure some parameters of your setup,
most notably - UART port (in my case - "/dev/tty.SLAB_USBtoUART").

In components submenu there's a few configuration settings related to OAP hardware setup (e.g. gpio pin assignments),
and "OAP Main" menu where you can change various functional parameters.
All settings are saved in sdkconfig file.

to flash the module and read from the serial output

	make flash monitor
	
That's it.

## First run

After booting the sensor for the first time, it will switch into Access Point mode and be visible as 

	ssid: OpenAirProject-XXXX
	pass: cleanair
	
After connecting to this network, open following url

	http://192.168.1.4

and configure your local WiFi network access. Sensor will reboot and connect to the wifi. If there's a need to reconfigure wifi settings, reboot the device with a button pressed down - it will switch into AP mode.

Happy DIY time!
	