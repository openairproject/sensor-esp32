/*
 * baycom.de
 *
 *  Created on: Jan 4, 2018
 *      Author: Deti
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

#include "http_get_publisher.h"

#include "oap_common.h"
#include "oap_storage.h"
#include "oap_debug.h"
#include "esp_request.h"
#include "bootwifi.h"
#include "cJSON.h"

extern const uint8_t _root_ca_pem_start[] asm("_binary_lets_encrypt_x3_cross_signed_pem_start");
extern const uint8_t _root_ca_pem_end[]   asm("_binary_lets_encrypt_x3_cross_signed_pem_end");

static const char *TAG = "http_get_publisher";

static char* url = NULL;
static char* sensorId = NULL;
static int _configured = 0;

static esp_err_t http_get(char* uri, oap_measurement_t* meas) {
	char* payload = malloc(512);
	if (!payload) return NULL;
	sprintf(payload, "%s?item=%s&type=oap", uri, sensorId);

	if (meas->pm) {
		sprintf(payload, "%s&pm1_0=%d&pm2_5=%d&pm10=%d", payload,
			meas->pm->pm1_0,
			meas->pm->pm2_5,
			meas->pm->pm10);
	}

	if (meas->env) {
		sprintf(payload, "%s&temp=%.1f&pressure=%.1f&humidity=%.0f", payload, 
			meas->env->temp,
			meas->env->sealevel,
			meas->env->humidity);
	}
	if (meas->co2) {
		sprintf(payload, "%s&co2=%d", payload, 
			meas->co2->co2);
	}

	request_t* req = req_new(payload);
	if (!req) {
		return ESP_FAIL;
	}
	ESP_LOGD(TAG, "request payload: %s", payload);
	req_setopt(req, REQ_SET_METHOD, "GET");
	req_setopt(req, REQ_SET_HEADER, HTTP_HEADER_CONNECTION_CLOSE);
	req_set_user_agent(req);
	
	req->ca_cert = req_parse_x509_crt((unsigned char*)_root_ca_pem_start, _root_ca_pem_end-_root_ca_pem_start);

	int response_code = req_perform(req);
	req_clean(req);
	free(payload);
	if (response_code == 200) {
		ESP_LOGI(TAG, "update succeeded");
		return ESP_OK;
	} else {
		ESP_LOGW(TAG, "update failed (response code: %d)", response_code);
		return ESP_FAIL;
	}

}

static esp_err_t http_get_publisher_configure(cJSON* config) {
	_configured = 0;
	ESP_LOGI(TAG, "http_get_publisher_configure");
	if (!config) {
		ESP_LOGI(TAG, "config not found");
		return ESP_FAIL;
	}

	cJSON* field;
	if ((field = cJSON_GetObjectItem(cJSON_GetObjectItem(config,"wifi"), "sensorId")) && field->valuestring && strlen(field->valuestring)) {
		set_config_str_field(&sensorId, field->valuestring);
	} else {
		ESP_LOGW(TAG, "sensorId not configured");
		return ESP_FAIL;
	}
	if ((field = cJSON_GetObjectItem(cJSON_GetObjectItem(config,"http_get_publisher"), "url")) && field->valuestring) {
		set_config_str_field(&url, field->valuestring);
	} else {
		ESP_LOGW(TAG, "url not configured");
		return ESP_FAIL;
	}
	if (!(field = cJSON_GetObjectItem(cJSON_GetObjectItem(config,"http_get_publisher"), "enabled")) || !field->valueint) {
		ESP_LOGI(TAG, "client disabled");
		return ESP_FAIL;
	}
	_configured = 1;
	return ESP_OK;
}

static esp_err_t http_get_publisher_send(oap_measurement_t* meas, oap_sensor_config_t* oap_sensor_config) {
	if (!_configured) {
		ESP_LOGE(TAG, "http_get_publisher not configured");
		return ESP_FAIL;
	}
	esp_err_t ret;
	if ((ret = wifi_connected_wait_for(5000)) != ESP_OK) {
		ESP_LOGW(TAG, "no connectivity, skip");
		return ret;
	}

	ret = http_get(url, meas);
	return ret;
}

oap_publisher_t http_get_publisher = {
	.name = "http_get_publisher",
	.configure = http_get_publisher_configure,
	.publish = &http_get_publisher_send
};
