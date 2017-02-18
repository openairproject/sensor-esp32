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

QueueHandle_t input_queue;

static const char *TAG = "awsiot";

static awsiot_config_t awsiot_config = {};

static void awsiot_task() {

	while (1) {
		awsiot_update_shadow(awsiot_config, NULL);
		vTaskDelay(5000);
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


QueueHandle_t awsiot_init()
{
	if (awsiot_configure(&awsiot_config) == ESP_OK) {
		input_queue = xQueueCreate(1, sizeof(oap_meas));
    	xTaskCreate(&awsiot_task, "awsiot_task", 1024*10, NULL, 5, NULL);
    	return input_queue;
	} else {
		release(awsiot_config);
		return NULL;
	}
}
