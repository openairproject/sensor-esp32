/*
 * test_thing_speak.c
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

#include "oap_test.h"
#include "thing_speak.h"

#define TEST_API_KEY "QMN6JJM996QXBORX"

TEST_CASE("publish to thingspeak", "[tspk]") {
	test_require_wifi();

	cJSON* cfg = cJSON_CreateObject();
	cJSON_AddNumberToObject(cfg, "enabled", 1);
	cJSON_AddStringToObject(cfg, "apikey", TEST_API_KEY);

	TEST_ESP_OK(thingspeak_publisher.configure(cfg));
	cJSON_Delete(cfg);

	pm_data_t pm = {
		.pm1_0 = 10,
		.pm2_5 = 25,
		.pm10 = 50,
		.sensor_idx = 0
	};

	pm_data_t pm_aux = {
		.pm1_0 = 11,
		.pm2_5 = 26,
		.pm10 = 51,
		.sensor_idx = 1
	};

	env_data_t env = {
		.temp = 15.5,
		.pressure = 999.9,
		.humidity = 79.11,
		.sensor_idx = 0
	};

	env_data_t env_int = {
		.temp = 22.1,
		.pressure = 997.9,
		.humidity = 43.89,
		.sensor_idx = 1
	};

	oap_measurement_t meas = {
		.pm = &pm,
		.pm_aux = &pm_aux,
		.env = &env,
		.env_int = &env_int,
		.local_time = 1505156826
	};

	oap_sensor_config_t sensor_config = {
		.0
	};

	TEST_ESP_OK(thingspeak_publisher.publish(&meas, &sensor_config));
}
