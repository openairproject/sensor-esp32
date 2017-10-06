/*
 * http.c
 *
 *  Created on: Feb 6, 2017
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

#include "thing_speak.h"
#include "oap_common.h"
#include "oap_storage.h"
#include "oap_debug.h"
#include "esp_request.h"

//to use https we'd need to install CA cert first.
#define OAP_THING_SPEAK_URI "http://api.thingspeak.com/update"

static const char *TAG = "thingspk";

static char* apikey = NULL;
static int _configured = 0;

static void set_config_str_field(char** field, char* value) {
	if (*field) {
		free(*field);
	}
	*field = str_dup(value);
}

static char* prepare_thingspeak_payload(oap_measurement_t* meas) {
	char* payload = malloc(512);
	if (!payload) return NULL;
	sprintf(payload, "api_key=%s", apikey);

	if (meas->pm) {
		sprintf(payload, "%s&field1=%d&field2=%d&field3=%d", payload,
			meas->pm->pm1_0,
			meas->pm->pm2_5,
			meas->pm->pm10);
	}

	if (meas->env) {
		sprintf(payload, "%s&field4=%.2f&field5=%.2f&field6=%.2f", payload,
			meas->env->temp,
			meas->env->pressure,
			meas->env->humidity);
	}

	if (meas->env_int) {
		sprintf(payload, "%s&field7=%.2f&field8=%.2f", payload,
			meas->env_int->temp,
			meas->env_int->humidity);
	}
	return payload;
}

static esp_err_t rest_post(char* uri, char* payload) {
	request_t* req = req_new(uri);
	if (!req) {
		return ESP_FAIL;
	}
	ESP_LOGD(TAG, "request payload: %s", payload);

	req_setopt(req, REQ_SET_POSTFIELDS, payload);
	req_setopt(req, REQ_SET_HEADER, HTTP_HEADER_CONNECTION_CLOSE);

	int response_code = req_perform(req);
	req_clean(req);
	if (response_code == 200) {
		ESP_LOGI(TAG, "update succeeded");
		return ESP_OK;
	} else {
		ESP_LOGW(TAG, "update failed (response code: %d)", response_code);
		return ESP_FAIL;
	}

}

static esp_err_t thing_speak_configure(cJSON* thingspeak) {
	_configured = 0;
	if (!thingspeak) {
		ESP_LOGI(TAG, "config not found");
		return ESP_FAIL;
	}

	cJSON* field;
	if (!(field = cJSON_GetObjectItem(thingspeak, "enabled")) || !field->valueint) {
		ESP_LOGI(TAG, "client disabled");
		return ESP_FAIL;
	}

	if ((field = cJSON_GetObjectItem(thingspeak, "apikey")) && field->valuestring) {
		set_config_str_field(&apikey, field->valuestring);
	} else {
		ESP_LOGW(TAG, "apikey not configured");
		return ESP_FAIL;
	}

	_configured = 1;
	return ESP_OK;
}

static esp_err_t thing_speak_send(oap_measurement_t* meas, oap_sensor_config_t* oap_sensor_config) {
	if (!_configured) {
		ESP_LOGE(TAG, "thingspeak not configured");
		return ESP_FAIL;
	}

	char* payload = prepare_thingspeak_payload(meas);
	if (payload) {
		esp_err_t ret = rest_post(OAP_THING_SPEAK_URI, payload);
		free(payload);
		return ret;
	} else {
		return ESP_FAIL;
	}
}

oap_publisher_t thingspeak_publisher = {
	.name = "ThingSpeak",
	.configure = thing_speak_configure,
	.publish = &thing_speak_send
};
