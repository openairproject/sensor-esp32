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

#ifndef MAIN_HW_GPIO_H_
#define MAIN_HW_GPIO_H_

#include "oap_common.h"
#include "oap_data_env.h"

typedef void(*env_callback)(env_data_t*);

typedef struct {
	uint8_t enabled;		//internal, read-only
	uint8_t sensor_idx;
	uint32_t interval;
	env_callback callback;
	uint8_t output_pin;
	uint8_t input_pin;
	QueueHandle_t gpio_evt_queue;
	time_t GPIlastLow;
	time_t GPIlastHigh;
	time_t GPOlastOut;
} hw_gpio_config_t;

/**
 * pm samples data is send to the queue.
 */
esp_err_t hw_gpio_init(hw_gpio_config_t* config);

/**
 * enable/disable sensor.
 */
esp_err_t hw_gpio_enable(hw_gpio_config_t* config, uint8_t enabled);
/**
 * fill config based on hardware configuration
 */
esp_err_t hw_gpio_set_hardware_config(hw_gpio_config_t* config, uint8_t sensor_idx);
/**
 * trigger output of gpio
 */
esp_err_t hw_gpio_send_trigger(hw_gpio_config_t* config, int value, int delay);

#endif /* MAIN_MHZ19_H_ */
