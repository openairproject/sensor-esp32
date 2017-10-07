/*
 * test_bmx280.c
 *
 *  Created on: Sep 21, 2017
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

#include "bmx280.h"
#include "unity.h"

esp_err_t bmx280_i2c_setup(bmx280_config_t* config);
esp_err_t bmx280_measurement_loop(bmx280_config_t* bmx280_config);
static env_data_t last_result;

static void collect_result(env_data_t* result) {
	memcpy(&last_result, result, sizeof(env_data_t));
}

static bmx280_config_t cfg = {
	.i2c_num = CONFIG_OAP_BMX280_I2C_NUM,
	.device_addr= CONFIG_OAP_BMX280_ADDRESS,
	.sda_pin= CONFIG_OAP_BMX280_I2C_SDA_PIN,
	.scl_pin= CONFIG_OAP_BMX280_I2C_SCL_PIN,
	.sensor_idx = 9,
	.interval = 0,	//no repeat
	.callback = &collect_result
};

TEST_CASE("bmx280 measurement","[bmx280]") {
	bzero(&last_result, 0);
	TEST_ASSERT_TRUE(CONFIG_OAP_BMX280_ENABLED);
	TEST_ESP_OK(bmx280_i2c_setup(&cfg));
	TEST_ESP_OK(bmx280_measurement_loop(&cfg));

	TEST_ASSERT_EQUAL_UINT(9, last_result.sensor_idx);
	TEST_ASSERT_TRUE_MESSAGE(last_result.temp > 10 && last_result.temp < 50, "invalid temperature");  //let's assume we do it indoors ;)
	TEST_ASSERT_TRUE_MESSAGE(last_result.pressure>850 && last_result.pressure < 1050, "invalid pressure");
	if (last_result.humidity != -1) {
		TEST_ASSERT_TRUE_MESSAGE(last_result.humidity > 0 && last_result.humidity < 100, "invalid humidity");  //bme280 only
	}
}
