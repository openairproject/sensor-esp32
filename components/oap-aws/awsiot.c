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
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "awsiot_common.h"
#include "awsiot_rest.h"
#include "oap_common.h"
#include "oap_debug.h"
#include "oap_publisher.h"
#include "oap_storage.h"
#include "bootwifi.h"

static const char *TAG = "awsiot";

static awsiot_config_t awsiot_config = {.0};
static int config_sent = 0;


static esp_err_t awsiot_rest_post(oap_measurement_t* meas, oap_sensor_config_t *sensor_config) {
	cJSON* shadow = cJSON_CreateObject();
	cJSON* state = cJSON_CreateObject();
	cJSON* reported = cJSON_CreateObject();
	cJSON* results = cJSON_CreateObject();
	cJSON* config = cJSON_CreateObject();

	cJSON* status = cJSON_CreateObject();

	cJSON_AddItemToObject(shadow, "state", state);
	cJSON_AddItemToObject(state, "reported", reported);
	cJSON_AddItemToObject(reported, "results", results);
	cJSON_AddItemToObject(reported, "config", config);
	cJSON_AddItemToObject(reported, "status", status);

	cJSON_AddNumberToObject(results, "uid", rand()); //what about 0?
	cJSON_AddNumberToObject(status, "heap", avg_free_heap_size());
	cJSON_AddNumberToObject(status, "heap_min", xPortGetMinimumEverFreeHeapSize());


	if (meas->local_time) {
		cJSON_AddNumberToObject(reported, "localTime", meas->local_time);
	} else {
		ESP_LOGW(TAG, "localTime not set, skip");
	}

	if (!config_sent) {
		//send it only once. of course if we want two-way configuration, we need versioning
		cJSON_AddBoolToObject(config, "indoor", sensor_config->indoor);
		cJSON_AddNumberToObject(config, "test", sensor_config->test);
		cJSON_AddStringToObject(config, "firmware", oap_version_str());
	}

	if (meas->pm) {
		cJSON* pm = cJSON_CreateObject();
		cJSON_AddItemToObject(results, "pm", pm);
		cJSON_AddNumberToObject(pm, "pm1_0", meas->pm->pm1_0);
		cJSON_AddNumberToObject(pm, "pm2_5", meas->pm->pm2_5);
		cJSON_AddNumberToObject(pm, "pm10", meas->pm->pm10);
		cJSON_AddNumberToObject(pm, "sensor", meas->pm->sensor_idx);
	} else {
		cJSON_AddNullToObject(results, "pm");
	}

	if (meas->pm_aux) {
		cJSON* pm = cJSON_CreateObject();
		cJSON_AddItemToObject(results, "pmAux", pm);
		cJSON_AddNumberToObject(pm, "pm1_0", meas->pm_aux->pm1_0);
		cJSON_AddNumberToObject(pm, "pm2_5", meas->pm_aux->pm2_5);
		cJSON_AddNumberToObject(pm, "pm10", meas->pm_aux->pm10);
		cJSON_AddNumberToObject(pm, "sensor", meas->pm_aux->sensor_idx);
	} else {
		cJSON_AddNullToObject(results, "pmAux");
	}

	if (meas->env) {
		cJSON* weather = cJSON_CreateObject();
		cJSON_AddItemToObject(results, "weather", weather);
		cJSON_AddNumberToObject(weather, "temp", meas->env->bmx280.temp);
		cJSON_AddNumberToObject(weather, "pressure", meas->env->bmx280.pressure);
		cJSON_AddNumberToObject(weather, "humidity", meas->env->bmx280.humidity);
		cJSON_AddNumberToObject(weather, "sensor", meas->env->sensor_idx);
	} else {
		cJSON_AddNullToObject(results, "weather");
	}

	if (meas->env_int) {
		cJSON* internal = cJSON_CreateObject();
		cJSON_AddItemToObject(results, "internal", internal);
		cJSON_AddNumberToObject(internal, "temp", meas->env_int->bmx280.temp);
		cJSON_AddNumberToObject(internal, "pressure", meas->env_int->bmx280.pressure);
		cJSON_AddNumberToObject(internal, "humidity", meas->env_int->bmx280.humidity);
		cJSON_AddNumberToObject(internal, "sensor", meas->env_int->sensor_idx);
	} else {
		cJSON_AddNullToObject(results, "internal");
	}

	char *body = cJSON_Print(shadow);

	cJSON_Delete(shadow);

	//ESP_LOGD(TAG, "shadow update: %s", body);
	esp_err_t res = awsiot_update_shadow(&awsiot_config, body);
	free(body);
	config_sent = config_sent || res == ESP_OK;
	return res;
}

static esp_err_t awsiot_configure(cJSON* awsiot) {
	awsiot_config.configured = 0;

	if (!awsiot) {
		ESP_LOGI(TAG, "config not found");
		return ESP_FAIL;
	}

	cJSON* field;
	if (!(field = cJSON_GetObjectItem(awsiot, "enabled")) || !field->valueint) {
		ESP_LOGI(TAG, "client disabled");
		return ESP_FAIL;
	}

	if ((field = cJSON_GetObjectItem(awsiot, "endpoint")) && field->valuestring) {
		set_config_str_field(&awsiot_config.endpoint, field->valuestring);
		ESP_LOGI(TAG, "endpoint: %s", awsiot_config.endpoint);
	} else {
		ESP_LOGE(TAG, "endpoint not configured");
		return ESP_FAIL;
	}

	if ((field = cJSON_GetObjectItem(awsiot, "port")) && field->valueint) {
		awsiot_config.port = field->valueint;
	} else {
		ESP_LOGE(TAG, "port not configured");
		return ESP_FAIL;
	}

	if ((field = cJSON_GetObjectItem(awsiot, "thingName")) && field->valuestring) {
		set_config_str_field(&awsiot_config.thingName, field->valuestring);
		ESP_LOGI(TAG, "thingName: %s", awsiot_config.thingName);
	} else {
		ESP_LOGE(TAG, "thingName not configured");
		return ESP_FAIL;
	}

	if ((field = cJSON_GetObjectItem(awsiot, "cert")) && field->valuestring) {
		set_config_str_field(&awsiot_config.cert, field->valuestring);
	} else {
		ESP_LOGE(TAG, "certificate not configured");
		return ESP_FAIL;
	}

	if ((field = cJSON_GetObjectItem(awsiot, "pkey")) && field->valuestring) {
		set_config_str_field(&awsiot_config.pkey,field->valuestring);
	} else {
		ESP_LOGE(TAG, "private key not configured");
		return ESP_FAIL;
	}
	awsiot_config.configured = 1;

	return ESP_OK;

}

static esp_err_t awsiot_send(oap_measurement_t* meas, oap_sensor_config_t *sensor_config) {
	if (!awsiot_config.configured) {
		ESP_LOGE(TAG, "awsiot not configured");
		return ESP_FAIL;
	}
	esp_err_t ret;
	if ((ret = wifi_connected_wait_for(5000)) != ESP_OK) {
		ESP_LOGW(TAG, "no connectivity, skip");
		return ret;
	}
	return awsiot_rest_post(meas, sensor_config);
}

oap_publisher_t awsiot_publisher = {
	.name = "AWSIoT",
	.configure = &awsiot_configure,
	.publish = &awsiot_send
};
