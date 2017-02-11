/*
 * pm_meter.h
 *
 *  Created on: Feb 10, 2017
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

#ifndef MAIN_PM_METER_H_
#define MAIN_PM_METER_H_

#include "freertos/queue.h"
#include "oap_common.h"

typedef enum {
	PM_MEAS_AVG,
	PM_MEAS_CONT
} pm_meas_mode;

void pm_meter_start(unsigned int warmingTime);
pm_data pm_meter_stop();
void pm_meter_init(QueueHandle_t samples_queue);

#endif /* MAIN_PM_METER_H_ */
