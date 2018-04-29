/*
 * mhz19.h
 *
 *  Created on: Jan 4, 2018
 *      Author: Deti
 *
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

#ifndef MAIN_MHZ19_H_
#define MAIN_MHZ19_H_

#include "oap_common.h"
#include "oap_data_env.h"
#include "driver/uart.h"

#define HW_MHZ19_DEVICES_MAX 1

typedef void(*env_callback)(env_data_t*);

typedef struct {
	uint8_t indoor;
	uint8_t enabled;		//internal, read-only
	uint8_t sensor_idx;
	uint32_t interval;
	env_callback callback;
	uart_port_t uart_num;
	uint8_t uart_txd_pin;
	uint8_t uart_rxd_pin;
	sma_data_t sma;
} mhz19_config_t;

extern mhz19_config_t mhz19_cfg[];

esp_err_t mhz19_init(mhz19_config_t* config);

/**
 * enable/disable sensor.
 */
esp_err_t mhz19_enable(mhz19_config_t* config, uint8_t enabled);


esp_err_t mhz19_calibrate(mhz19_config_t* config);

/**
 * fill config based on hardware configuration
 */
esp_err_t mhz19_set_hardware_config(mhz19_config_t* config, uint8_t sensor_idx);

#endif /* MAIN_MHZ19_H_ */
