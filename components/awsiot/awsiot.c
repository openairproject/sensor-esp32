/*
 * awsiot_client.c
 *
 *  Created on: Feb 18, 2017
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

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "awsiot_common.h"
#include "awsiot_rest.h"
#include "oap_storage.h"
#include "oap_common.h"
#include "cJSON.h"


QueueHandle_t input_queue;

static const char *TAG = "awsiot";

static awsiot_config_t awsiot_config = {};
static oap_sensor_config_t sensor_config;

static void awsiot_task() {
	oap_meas meas;
	int updated = 0;

	while (1) {
		if(xQueuePeek(input_queue, &meas, 1000)) {

			cJSON* shadow = cJSON_CreateObject();
			cJSON* state = cJSON_CreateObject();
			cJSON* reported = cJSON_CreateObject();
			cJSON* results = cJSON_CreateObject();
			cJSON* config = cJSON_CreateObject();
			cJSON* pm = cJSON_CreateObject();
			cJSON* weather = cJSON_CreateObject();


			cJSON_AddItemToObject(shadow, "state", state);
			cJSON_AddItemToObject(state, "reported", reported);
			cJSON_AddItemToObject(reported, "results", results);
			cJSON_AddItemToObject(reported, "config", config);

			cJSON_AddItemToObject(results, "pm", pm);
			cJSON_AddItemToObject(results, "weather", weather);

			if (meas.local_time) {
				cJSON_AddNumberToObject(reported, "localTime", meas.local_time);
			} else {
				ESP_LOGW(TAG, "localTime not set, skip");
			}

			if (!updated) {
				//send it only once. of course if we want two-way configuration, we need versioning
				cJSON_AddBoolToObject(config, "indoor", sensor_config.indoor);
				cJSON_AddNumberToObject(config, "test", sensor_config.test);
			}

			cJSON_AddNumberToObject(pm, "pm1_0", meas.pm.pm1_0);
			cJSON_AddNumberToObject(pm, "pm2_5", meas.pm.pm2_5);
			cJSON_AddNumberToObject(pm, "pm10", meas.pm.pm10);

			cJSON_AddNumberToObject(weather, "temp", meas.env.temp);
			cJSON_AddNumberToObject(weather, "pressure", meas.env.pressure);
			cJSON_AddNumberToObject(weather, "humidity", meas.env.humidity);

			char *body = cJSON_Print(shadow);

			cJSON_Delete(shadow);

			ESP_LOGD(TAG, "shadow update: %s", body);

			esp_err_t res = awsiot_update_shadow(awsiot_config, body);
			free(body);

			if (res == ESP_OK) {
				ESP_LOGI(TAG, "data sent successfully");
				xQueueReceive(input_queue, &meas, 1000);
				updated = 1;
			} else {
				ESP_LOGW(TAG, "data post failed");
				vTaskDelay(5000 / portTICK_PERIOD_MS);
			}
		}
	}
}

static void release(awsiot_config_t awsiot_config) {
	if (awsiot_config.thingName) free(awsiot_config.thingName);
	if (awsiot_config.cert) free(awsiot_config.cert);
	if (awsiot_config.pkey) free(awsiot_config.pkey);
}

static esp_err_t awsiot_configure(awsiot_config_t* awsiot_config) {
	cJSON* awsiot = storage_get_config("awsiot");
	if (!awsiot) {
		ESP_LOGI(TAG, "config not found");
		return ESP_FAIL;
	}

	cJSON* field;
	if (!(field = cJSON_GetObjectItem(awsiot, "enabled")) || !field->valueint) {
		ESP_LOGI(TAG, "client disabled");
		return ESP_FAIL;
	}

	if ((field = cJSON_GetObjectItem(awsiot, "thingName")) && field->valuestring) {
		awsiot_config->thingName = malloc(strlen(field->valuestring)+1);
		strcpy(awsiot_config->thingName,field->valuestring);
		ESP_LOGI(TAG, "thingName: %s", awsiot_config->thingName);
	} else {
		ESP_LOGW(TAG, "apikey not configured");
		return ESP_FAIL;
	}

	if ((field = cJSON_GetObjectItem(awsiot, "cert")) && field->valuestring) {
		awsiot_config->cert = malloc(strlen(field->valuestring)+1);
		strcpy(awsiot_config->cert,field->valuestring);
	} else {
		ESP_LOGW(TAG, "cert not configured");
		return ESP_FAIL;
	}

	if ((field = cJSON_GetObjectItem(awsiot, "pkey")) && field->valuestring) {
		awsiot_config->pkey = malloc(strlen(field->valuestring)+1);
		strcpy(awsiot_config->pkey,field->valuestring);
	} else {
		ESP_LOGW(TAG, "pkey not configured");
		return ESP_FAIL;
	}

	return ESP_OK;
}

QueueHandle_t awsiot_init(oap_sensor_config_t _sensor_config)
{
	if (awsiot_configure(&awsiot_config) == ESP_OK) {
		sensor_config = _sensor_config;
		input_queue = xQueueCreate(1, sizeof(oap_meas));
    	xTaskCreate(&awsiot_task, "awsiot_task", 1024*10, NULL, 5, NULL);
    	return input_queue;
	} else {
		release(awsiot_config);
		return NULL;
	}
}
