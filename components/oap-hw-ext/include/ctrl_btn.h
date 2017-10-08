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

typedef enum {
	SINGLE_CLICK,
	MANY_CLICKS,
	TOO_MANY_CLICKS
} btn_action_t;
typedef void(*btn_callback_f)(btn_action_t);

bool is_ap_mode_pressed();
esp_err_t btn_configure(btn_callback_f callback);

#endif /* MAIN_BTN_H_ */
