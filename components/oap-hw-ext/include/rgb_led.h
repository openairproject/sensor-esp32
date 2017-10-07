/*
 * led.h
 *
 *  Created on: Feb 7, 2017
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

#ifndef MAIN_LED_H_
#define MAIN_LED_H_

#include "freertos/queue.h"

typedef struct { float v[3]; } rgb_color_t;

typedef enum {
	LED_SET = 0,
	LED_FADE_TO = 1,
	LED_BLINK = 2,
	LED_PULSE = 3
} led_mode_t;

typedef struct {
	led_mode_t mode;
	rgb_color_t color;
	uint32_t freq;
} led_cmd_t;

void led_init(int enabled, xQueueHandle cmd_queue);

#endif /* MAIN_LED_H_ */
