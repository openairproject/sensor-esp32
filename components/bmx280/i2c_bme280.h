/*
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

esp_err_t BME280_verify_chip();

esp_err_t BME280_init(uint8_t operation_mode, uint8_t i2c_num, uint8_t device_addr);

esp_err_t BME280_read();

env_data BME280_last_result();

#endif
