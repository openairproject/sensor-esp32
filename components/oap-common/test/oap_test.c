/*
 * oap_test.c
 *
 *  Created on: Sep 11, 2017
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

#include "esp_log.h"
#include "oap_test.h"
#include "bootwifi.h"
#include "test_wifi.h"
#include "unity.h"

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>

static const char* TAG = "test";

extern oc_wifi_t oap_wifi_config;

void test_require_wifi() {
	test_require_wifi_with(NULL);
}

void test_require_wifi_with(wifi_state_callback_f wifi_state_callback) {
	memset(&oap_wifi_config, 0, sizeof(oap_wifi_config));
	strcpy(oap_wifi_config.ssid, OAP_TEST_WIFI_SSID);
	strcpy(oap_wifi_config.password,OAP_TEST_WIFI_PASSWORD);
	oap_wifi_config.callback = wifi_state_callback;
	wifi_boot();
	TEST_ESP_OK(wifi_connected_wait_for(10000));
	ESP_LOGI(TAG, "connected sta");
}

void test_require_ap() {
	test_require_ap_with(NULL);
}

void test_require_ap_with(wifi_state_callback_f wifi_state_callback) {
	memset(&oap_wifi_config, 0, sizeof(oap_wifi_config));
	oap_wifi_config.ap_mode=1;
	oap_wifi_config.callback = wifi_state_callback;
	wifi_boot();
	TEST_ESP_OK(wifi_ap_started_wait_for(10000));
	ESP_LOGI(TAG, "connected ap");
}

static uint32_t IRAM_ATTR time_now()
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

int test_timeout(test_timer_t* t) {
	if (t->started <= 0) t->started = time_now();
	return time_now() - t->started > t->wait_for;
}

void test_delay(uint32_t ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

static void configure_gpio(uint8_t gpio) {
	if (gpio > 0) {
		ESP_LOGD(TAG, "configure pin %d as output", gpio);
		gpio_pad_select_gpio(gpio);
		ESP_ERROR_CHECK(gpio_set_direction(gpio, GPIO_MODE_OUTPUT));
		ESP_ERROR_CHECK(gpio_set_pull_mode(gpio, GPIO_PULLDOWN_ONLY));
	}
}

void test_reset_hw() {
	ESP_LOGI(TAG,"reset peripherals");
	configure_gpio(GPIO_NUM_10);	//disable pm1
	configure_gpio(GPIO_NUM_2);		//disable pm2
}
