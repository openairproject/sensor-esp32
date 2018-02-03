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

#ifndef MAIN_HCSR04_H_
#define MAIN_HCSR04_H_

#include "oap_common.h"
#include "oap_data_env.h"

typedef void(*env_callback)(env_data_t*);

typedef enum {
        IDLE = 0,
        WAITFORECHO,
        RECEIVE
} hcsr04_state_t;

typedef struct {
	uint8_t enabled;		//internal, read-only
	uint8_t sensor_idx;
	uint32_t interval;
	env_callback callback;
	uint8_t trigger_pin;
	uint8_t echo_pin;
	hcsr04_state_t state;
	QueueHandle_t hcsr04_evt_queue;
	sma_data_t sma;
} hcsr04_config_t;

/**
 * pm samples data is send to the queue.
 */
esp_err_t hcsr04_init(hcsr04_config_t* config);

/**
 * enable/disable sensor.
 */
esp_err_t hcsr04_enable(hcsr04_config_t* config, uint8_t enabled);


/**
 * fill config based on hardware configuration
 */
esp_err_t hcsr04_set_hardware_config(hcsr04_config_t* config, uint8_t sensor_idx);

#endif /* MAIN_MHZ19_H_ */
