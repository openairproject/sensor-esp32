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
#include "oap_storage.h"
#include "awsiot.h"

static const char *TAG = "app";

//static QueueHandle_t result_queue;
static QueueHandle_t led_queue;
static QueueHandle_t pm_queue;

//static void printTime() {
//	struct timeval tv;
////	time_t nowtime;
////	struct tm *nowtm;
////	char tmbuf[64], buf[64];
//
//	gettimeofday(&tv, NULL);
//	//nowtime = tv.tv_sec;
//	//nowtm = localtime(&nowtime);
//	//strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d %H:%M:%S", nowtm);
//	//printf(buf, sizeof buf, "%s.%06d", tmbuf, tv.tv_usec);
//	ESP_LOGI(TAG, "timestamp: %ld", tv.tv_sec);
//}

static oap_sensor_config_t sensor_config;

static oap_sensor_config_t get_config() {
	oap_sensor_config_t sensor_config = {};
	ESP_LOGD(TAG, "retrieve sensor config");
	cJSON* sensor = storage_get_config("sensor");
	cJSON* sconfig = cJSON_GetObjectItem(sensor, "config");
	cJSON* field;

	if ((field = cJSON_GetObjectItem(sconfig, "led"))) sensor_config.led = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "indoor"))) sensor_config.indoor = field->valueint;
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
	pm_meter_init(pms_init(sensor_config.indoor));
	while (1) {
		update_led_mode(LED_PULSE);
		pm_meter_start(sensor_config.warmUpTime);
		vTaskDelay(sensor_config.measTime * 1000 / portTICK_PERIOD_MS);
		pm_data pm = pm_meter_stop();
		update_led_mode(LED_SET);
		xQueueSend(pm_queue, &pm, 1000 / portTICK_PERIOD_MS); //1sec
		vTaskDelay((sensor_config.measInterval-sensor_config.measTime) * 1000 / portTICK_PERIOD_MS);
	}
}

static void main_task() {
	pm_queue = xQueueCreate(1, sizeof(pm_data));

	//buffer up to 100 measurements
	//result_queue = xQueueCreate(CONFIG_OAP_RESULT_BUFFER_SIZE, sizeof(oap_meas));
	led_queue = xQueueCreate(10, sizeof(led_cmd));

	QueueHandle_t thing_speak_queue = thing_speak_init();
	QueueHandle_t awsiot_queue = awsiot_init(sensor_config);

//	while (1) {
//		oap_meas meas = {};
//		if (!xQueueSend(awsiot_queue, &meas, 1)) {
//			ESP_LOGW(TAG,"awsiot_queue queue overflow");
//		}
//		vTaskDelay(1000);
//	}

	QueueHandle_t env_queue = bmx280_init();
	led_init(get_config().led, led_queue);
	update_led();

	gpio_num_t btn_gpio[] = {CONFIG_OAP_BTN_0_PIN};
	//xQueueHandle btn_events = btn_init(btn_gpio, sizeof(btn_gpio)/sizeof(btn_gpio[0]));
//	btn_event be;
//	while (1) {
//		if(xQueueReceive(btn_events, &be, 1000)) {
//			ESP_LOGI(TAG, "btn [%d] = %d", be.index, be.level);
//		}
//	}

	env_data env = {};
	long env_timestamp = 0;

	while (1) {
		long localTime = oap_epoch_sec_valid();
		if (env_queue != NULL && xQueueReceive(env_queue, &env, 100)) {
			env_timestamp = localTime;
			ESP_LOGI(TAG,"Temperature : %.2f C, Pressure: %.2f hPa, Humidity: %.2f %%", env.temp, env.pressure, env.humidity);
		}

		pm_data pm;
		if (xQueueReceive(pm_queue, &pm, 100)) {
			float aqi = fminf(pm.pm2_5 / 100.0, 1.0);
			ESP_LOGI(TAG, "AQI=%f",aqi);

			//for error we can: led_state.freq = 200; LED_BLINK
			update_led_color(LED_SET, aqi,(1-aqi), 0);

			oap_meas meas = {
				.pm = pm,
				.env = env,			//TODO allow null, check last timestamp
				.local_time = localTime
			};

			//broadcast
			if (thing_speak_queue) {
				if (!xQueueSend(thing_speak_queue, &meas, 1)) {
					ESP_LOGW(TAG,"thing_speak_queue queue overflow");
				}
			}
			if (awsiot_queue) {
				if (!xQueueSend(awsiot_queue, &meas, 1)) {
					ESP_LOGW(TAG,"awsiot_queue queue overflow");
				}
			}
		}
	}
}

void app_main()
{
	vTaskDelay(1000 / portTICK_PERIOD_MS);
	ESP_LOGI(TAG,"starting app...");

	storage_init();
	sensor_config = get_config();

	//wifi/mongoose requires plenty of mem, start it here
	bootWiFi();
	xTaskCreate(main_task, "main_task", 1024*10, NULL, 10, NULL);
	xTaskCreate(pm_meter_trigger_task, "pm_meter_trigger_task", 1024*8, NULL, 10, NULL);
	while (1) {
		vTaskDelay(10000 / portTICK_PERIOD_MS);
	}
}
