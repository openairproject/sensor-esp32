/*
The driver for the temperature, pressure, & humidity sensor BME280
Official repository: https://github.com/RyAndrew/esp8266_i2c_bme280
Adapted From: https://github.com/CHERTS/esp8266-i2c_bmp180
This driver depends on the I2C driver https://github.com/zarya/esp8266_i2c_driver/

The MIT License (MIT)

Copyright (C) 2015 Andrew Rymarczyk

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*  This file is part of OpenAirProject-ESP32.
*
*  OpenAirProject-ESP32 is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 3 of the License, or
*  (at your option) any later version.
*
*  OpenAirProject-ESP32 is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with OpenAirProject-ESP32.  If not, see <http://www.gnu.org/licenses/>.

*/

#ifndef __I2C_BME280_H
#define	__I2C_BME280_H

#include <driver/i2c.h>
#include <esp_log.h>
#include "oap_common.h"



#define BME280_MODE_NORMAL			0x03 //reads sensors at set interval
#define BME280_MODE_FORCED			0x01 //reads sensors once when you write this register

//#define BME280_DEBUG 1 //uncomment for debugging messages

bool BME280_Init(uint8_t operationMode);

void BME280_readSensorData(void);

env_data BME280_data();

#endif
