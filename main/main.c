/*
 * main.c
 *
 *  Created on: Feb 3, 2017
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <math.h>

#include "thing_speak.h"
#include "bootwifi.h"
#include "rgb_led.h"
#include "ctrl_btn.h"
#include "bmx280.h"
#include "pmsx003.h"
#include "pm_meter.h"
#include "oap_common.h"
#include "oap_storage.h"
#include "oap_debug.h"
#include "awsiot.h"

static const char *TAG = "app";

static QueueHandle_t led_queue;
static QueueHandle_t pm_queue;

static oap_sensor_config_t sensor_config;

static oap_sensor_config_t get_config() {
	oap_sensor_config_t sensor_config = {};
	ESP_LOGD(TAG, "retrieve sensor config");
	cJSON* sensor = storage_get_config("sensor");
	cJSON* sconfig = cJSON_GetObjectItem(sensor, "config");
	cJSON* field;

	if ((field = cJSON_GetObjectItem(sconfig, "led"))) sensor_config.led = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "indoor"))) sensor_config.indoor = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "fan"))) sensor_config.fan = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "heater"))) sensor_config.heater = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "warmUpTime"))) sensor_config.warmUpTime = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "measTime"))) sensor_config.measTime = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "measInterval"))) sensor_config.measInterval = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "measStrategy"))) sensor_config.measStrategy = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "test"))) sensor_config.test = field->valueint;
	return sensor_config;
}

static led_cmd led_state = {
	.color = {.v = {0,0,1}} //initial color (when no samples collected)
};
static void update_led() {
	xQueueSend(led_queue, &led_state, 100);
}
static void update_led_mode(led_mode mode) {
	led_state.mode = mode;
	update_led();
}

static void update_led_color(led_mode mode, float r, float g, float b) {
	led_state.color.v[0] = r;
	led_state.color.v[1] = g;
	led_state.color.v[2] = b;
	update_led_mode(mode);
}

static void pm_meter_trigger_task() {

	pms_config_t* pms_config = malloc(sizeof(pms_config_t));
	memset(pms_config, 0, sizeof(pms_config_t));
	pms_config->indoor = sensor_config.indoor;
	pms_config->set_pin = CONFIG_OAP_PM_SENSOR_CONTROL_PIN;
	pms_config->heater_pin = sensor_config.heater ? CONFIG_OAP_HEATER_CONTROL_PIN : 0;
	pms_config->fan_pin = sensor_config.fan ? CONFIG_OAP_FAN_CONTROL_PIN : 0;

	pms_config->uart_num = CONFIG_OAP_PM_UART_NUM;
	pms_config->uart_txd_pin = CONFIG_OAP_PM_UART_TXD_PIN;
	pms_config->uart_rxd_pin = CONFIG_OAP_PM_UART_RXD_PIN;
	pms_config->uart_rts_pin = CONFIG_OAP_PM_UART_RTS_PIN;
	pms_config->uart_cts_pin = CONFIG_OAP_PM_UART_CTS_PIN;
	pms_config->callback = &pm_meter_collect;

	pms_init(pms_config);
	pm_meter_init(pms_config);

	int delay_sec = (sensor_config.measInterval-sensor_config.measTime);
	while (1) {
		log_task_stack("pm_meter_trigger");
		update_led_mode(LED_PULSE);
		pm_meter_start(sensor_config.warmUpTime);
		delay(sensor_config.measTime * 1000);
		pm_data pm = pm_meter_sample(delay_sec>0);

		float aqi = fminf(pm.pm2_5 / 100.0, 1.0);
		ESP_LOGI(TAG, "AQI=%f",aqi);
		update_led_color(LED_SET, aqi,(1-aqi), 0);

		xQueueSend(pm_queue, &pm, 1000 / portTICK_PERIOD_MS); //1sec
		if (delay_sec > 0) {
			delay(delay_sec * 1000);
		} else {
			delay(10);
		}
	}
}

static env_data env = {0};
static env_data env_int = {0};

static void bmx280_callback(env_data* result) {
	ESP_LOGI(TAG,"Env (%d): Temperature : %.2f C, Pressure: %.2f hPa, Humidity: %.2f %%", result->sensor, result->temp, result->pressure, result->humidity);
	memcpy(result->sensor ? &env_int : &env, result, sizeof(env_data));
}

static void main_task() {
	led_init(get_config().led, led_queue);
	update_led();

	if (CONFIG_OAP_BMX280_ENABLED) {
		bmx280_config_t *bmx280_config = malloc(sizeof(bmx280_config_t));

		bmx280_config->i2c_num = CONFIG_OAP_BMX280_I2C_NUM;
		bmx280_config->device_addr = CONFIG_OAP_BMX280_ADDRESS;
		bmx280_config->sda_pin = CONFIG_OAP_BMX280_I2C_SDA_PIN;
		bmx280_config->scl_pin = CONFIG_OAP_BMX280_I2C_SCL_PIN;
		bmx280_config->sensor = 0;
		bmx280_config->interval = 5000;
		bmx280_config->callback = &bmx280_callback;

		if (bmx280_init(bmx280_config) != ESP_OK) {
			ESP_LOGE(TAG, "couldn't initialise bmx280 sensor 0");
		}
	}

	if (CONFIG_OAP_BMX280_ENABLED_AUX) {
		bmx280_config_t *bmx280_config = malloc(sizeof(bmx280_config_t));

		bmx280_config->i2c_num = CONFIG_OAP_BMX280_I2C_NUM_AUX;
		bmx280_config->device_addr = CONFIG_OAP_BMX280_ADDRESS_AUX;
		bmx280_config->sda_pin = CONFIG_OAP_BMX280_I2C_SDA_PIN_AUX;
		bmx280_config->scl_pin = CONFIG_OAP_BMX280_I2C_SCL_PIN_AUX;
		bmx280_config->sensor = 1;
		bmx280_config->interval = 5000;
		bmx280_config->callback = &bmx280_callback;

		if (bmx280_init(bmx280_config) != ESP_OK) {
			ESP_LOGE(TAG, "couldn't initialise bmx280 sensor 1");
		}
	}


	gpio_num_t btn_gpio[] = {CONFIG_OAP_BTN_0_PIN};
	//xQueueHandle btn_events = btn_init(btn_gpio, sizeof(btn_gpio)/sizeof(btn_gpio[0]));
//	btn_event be;
//	while (1) {
//		if(xQueueReceive(btn_events, &be, 1000)) {
//			ESP_LOGI(TAG, "btn [%d] = %d", be.index, be.level);
//		}
//	}

	//env_data env = {};
	//long env_timestamp = 0;

	while (1) {
		long localTime = oap_epoch_sec_valid();
		pm_data pm;
		if (xQueueReceive(pm_queue, &pm, 100)) {
			log_task_stack("main_task");

			oap_meas meas = {
				.pm = pm,
				.env = env,			//TODO allow null, check last timestamp
				.env_int = env_int,
				.local_time = localTime
			};

			thing_speak_send(&meas);
			awsiot_send(&meas, &sensor_config);
		}
	}
}

void app_main()
{
	delay(1000);
	ESP_LOGI(TAG,"starting app...");

	storage_init();
	sensor_config = get_config();

	//wifi/mongoose requires plenty of mem, start it here
	bootWiFi();
	pm_queue = xQueueCreate(1, sizeof(pm_data));
	led_queue = xQueueCreate(10, sizeof(led_cmd));

	//xTaskCreate(main_task, "main_task", 1024*4, NULL, DEFAULT_TASK_PRIORITY, NULL);
	xTaskCreate(pm_meter_trigger_task, "pm_meter_trigger_task", 1024*4, NULL, DEFAULT_TASK_PRIORITY, NULL);
	main_task();
}
