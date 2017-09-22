/*
 * test_pmsx003.c
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


#include "pmsx003.h"
#include "oap_test.h"

static pm_data last_result;

static int got_result = 0;
static void pms_test_callback(pm_data* result) {
	got_result = 1;
	memcpy(&last_result, result, sizeof(pm_data));
}

static pms_config_t config = {
	.indoor = 1,
	.enabled = 1,
	.sensor = 7,
	.callback = pms_test_callback,
	.set_pin = CONFIG_OAP_PM_SENSOR_CONTROL_PIN,
	.heater_pin = 0,
	.fan_pin = 0,
	.heater_enabled = 0,
	.fan_enabled = 0,
	.uart_num = CONFIG_OAP_PM_UART_NUM,
	.uart_txd_pin = CONFIG_OAP_PM_UART_TXD_PIN,
	.uart_rxd_pin = CONFIG_OAP_PM_UART_RXD_PIN,
	.uart_rts_pin = CONFIG_OAP_PM_UART_RTS_PIN,
	.uart_cts_pin = CONFIG_OAP_PM_UART_CTS_PIN
};

esp_err_t pms_init_uart(pms_config_t* config);
void pms_init_gpio(pms_config_t* config);
esp_err_t pms_uart_read(pms_config_t* config, uint8_t data[32]);

static int uart_installed = 0;

TEST_CASE("pmsx003 measurement","pmsx003") {
	pms_init_gpio(&config);
	if (!uart_installed) {
		TEST_ESP_OK(pms_init_uart(&config));
		uart_installed = 1;
	}
	got_result = 0;
	uint8_t data[32];

	test_timer_t t = {
		.started = 0,
		.wait_for = 10000 //it takes a while to spin it up
	};

	TEST_ESP_OK(pms_enable(&config, 1));
	while (!got_result && !test_timeout(&t)) {
		pms_uart_read(&config, data);
	}
	TEST_ESP_OK(pms_enable(&config, 0));
	TEST_ASSERT_TRUE_MESSAGE(got_result, "timeout while waiting for measurement");

	TEST_ASSERT_EQUAL_INT(config.sensor, last_result.sensor);
	TEST_ASSERT_TRUE_MESSAGE(last_result.pm1_0 > 0, "no pm1.0");
	TEST_ASSERT_TRUE_MESSAGE(last_result.pm2_5 > 0, "no pm2.5");
	TEST_ASSERT_TRUE_MESSAGE(last_result.pm10 > 0, "no pm10");
}


