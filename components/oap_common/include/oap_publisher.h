/*
 * oap_publisher.h
 *
 *  Created on: Oct 1, 2017
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

#ifndef COMPONENTS_OAP_COMMON_INCLUDE_OAP_PUBLISHER_H_
#define COMPONENTS_OAP_COMMON_INCLUDE_OAP_PUBLISHER_H_

#include "cJSON.h"
#include "oap_data.h"
#include "esp_err.h"

typedef esp_err_t(*oap_publisher_configure_f)(cJSON* config);
typedef esp_err_t(*oap_publisher_publish_f)(oap_measurement_t* meas, oap_sensor_config_t* sensor_config);

typedef struct {
	char* name;
	oap_publisher_configure_f configure;
	oap_publisher_publish_f publish;
} oap_publisher_t;

#endif /* COMPONENTS_OAP_COMMON_INCLUDE_OAP_PUBLISHER_H_ */
