/*
 * pms.h
 *
 *  Created on: Feb 3, 2017
 *      Author: kris
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

#ifndef MAIN_PMS_H_
#define MAIN_PMS_H_

#include "oap_common.h"
#include "driver/uart.h"

typedef void(*pms_callback)(uint8_t sensor, pm_data*);

typedef struct pms_config_t {
	uint8_t indoor;
	uint8_t enabled;
	uint8_t sensor;
	pms_callback callback;
	uint8_t set_pin;
	uint8_t heater_pin;
	uint8_t fan_pin;
	uint8_t heater_enabled;
	uint8_t fan_enabled;

	uart_port_t uart_num;
	uint8_t uart_txd_pin;
	uint8_t uart_rxd_pin;
	uint8_t uart_rts_pin;
	uint8_t uart_cts_pin;
} pms_config_t;

/**
 * pm samples data is send to the queue.
 */
esp_err_t pms_init(pms_config_t* config);

/**
 * enable/disable sensor.
 */
esp_err_t pms_enable(pms_config_t* config, uint8_t enabled);

#endif /* MAIN_PMS_H_ */
