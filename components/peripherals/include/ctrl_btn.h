/*
 * btn.h
 *
 *  Created on: Feb 4, 2017
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

#ifndef MAIN_BTN_H_
#define MAIN_BTN_H_

#include "freertos/queue.h"
#include "driver/gpio.h"

typedef struct {
	uint8_t 	index;
	gpio_num_t 	gpio;
	uint8_t 	level;
} btn_event;

xQueueHandle btn_init(gpio_num_t* btns_gpio, uint8_t num_of_btns);

#endif /* MAIN_BTN_H_ */
