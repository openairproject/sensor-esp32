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
#include "oap_data_env.h"

#define BME280_MODE_NORMAL			0x03 //reads sensors at set interval
#define BME280_MODE_FORCED			0x01 //reads sensors once when you write this register

#define CHIP_TYPE_BMP 				1
#define CHIP_TYPE_BME 				2

#define HUMIDITY_MEAS_UNSUPPORTED 	-1

typedef struct bmx280_calib_t {
	uint16_t dig_T1;
	int16_t dig_T2;
	int16_t dig_T3;
	uint16_t dig_P1;
	int16_t dig_P2;
	int16_t dig_P3;
	int16_t dig_P4;
	int16_t dig_P5;
	int16_t dig_P6;
	int16_t dig_P7;
	int16_t dig_P8;
	int16_t dig_P9;
	int8_t  dig_H1;
	int16_t dig_H2;
	int8_t  dig_H3;
	int16_t dig_H4;
	int16_t dig_H5;
	int8_t  dig_H6;
} bmx280_calib_t;

typedef struct i2c_comm_t {
	uint8_t i2c_num;
	uint8_t device_addr;
} i2c_comm_t;

typedef struct bme280_sensor_t {
	uint8_t operation_mode;
	i2c_comm_t i2c_comm;
	bmx280_calib_t calib;
	uint8_t chip_type;
} bme280_sensor_t;

esp_err_t BME280_verify_chip(bme280_sensor_t* bme280_sensor);

esp_err_t BME280_init(bme280_sensor_t* bme280_sensor);

esp_err_t BME280_read(bme280_sensor_t* bme280_sensor, env_data_t* result);

#endif
