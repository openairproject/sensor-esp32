/*
 * oap_common.c
 *
 *  Created on: Feb 11, 2017
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

#include <stdlib.h>
#include <string.h>

#include <esp_log.h>
#include <esp_err.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>
#include "cJSON.h"

extern const uint8_t default_config_json_start[] asm("_binary_default_config_json_start");
extern const uint8_t default_config_json_end[] asm("_binary_default_config_json_end");

static char* NAMESPACE = "OAP";
static char* TAG = "storage";
static char* PASSWORD_NOT_CHANGED = "<not-changed>";

int storage_get_blob(const char* key, void* out_value, size_t length) {
	nvs_handle handle;
	esp_err_t err;
	err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "nvs_open: %x", err);
		return err;
	}
	err = nvs_get_blob(handle, key, out_value, &length);
	if (err != ESP_OK) {
		ESP_LOGD(TAG, "No connection record found (%d).", err);
		nvs_close(handle);
		return err;
	}
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "nvs_open: %x", err);
		nvs_close(handle);
		return err;
	}
	nvs_close(handle);
	return ESP_OK;
}

int storage_get_str(const char* key, char** out_value) {
	nvs_handle handle;
	esp_err_t err;
	err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "nvs_open: %x", err);
		return err;
	}
	size_t length;
	err = nvs_get_str(handle, key, 0, &length);
	if (err != ESP_OK) {
		ESP_LOGD(TAG, "No connection record found(1) (%d).", err);
		nvs_close(handle);
		return err;
	}
	*out_value = malloc(length);

	err = nvs_get_str(handle, key, *out_value, &length);
	if (err != ESP_OK) {
		ESP_LOGD(TAG, "No connection record found(2) (%d).", err);
		nvs_close(handle);
		return err;
	}

	if (err != ESP_OK) {
		ESP_LOGE(TAG, "nvs_open: %x", err);
		nvs_close(handle);
		return err;
	}
	nvs_close(handle);
	return ESP_OK;
}

void storage_put_blob(const char* key, void* value, size_t length) {
	nvs_handle handle;
	ESP_ERROR_CHECK(nvs_open(NAMESPACE, NVS_READWRITE, &handle));
	ESP_ERROR_CHECK(nvs_set_blob(handle, key, value, length));
	ESP_ERROR_CHECK(nvs_commit(handle));
	nvs_close(handle);
}

void storage_put_str(const char* key, char* value) {
	nvs_handle handle;
	ESP_ERROR_CHECK(nvs_open(NAMESPACE, NVS_READWRITE, &handle));
	ESP_ERROR_CHECK(nvs_set_str(handle, key, value));
	ESP_ERROR_CHECK(nvs_commit(handle));
	nvs_close(handle);
}

//----------- config --------------

static cJSON* _config;

cJSON* storage_get_config(const char* module) {
	if (!_config) {
		ESP_LOGE(TAG, "call storage_init_config() first!");
		abort();
	}
	if (!module) {
		return _config;
	} else {
		return cJSON_GetObjectItem(_config, module);
	}
}

static void storage_set_config(cJSON *config) {
	if (_config) cJSON_Delete(_config);
	_config = config;
	char* _configStr = cJSON_Print(_config);
	storage_put_str("config", _configStr);
	free(_configStr);
}

/*
 * returns entire config as a string.
 * free result after use.
 */
char* storage_get_config_str() {
	cJSON* copy = cJSON_Duplicate(_config, 1);
	cJSON* wifi = cJSON_GetObjectItem(copy, "wifi");
	//replace existing password (security)
	if (wifi && cJSON_GetObjectItem(wifi, "password")) {
		cJSON_DeleteItemFromObject(wifi, "password");
		cJSON_AddStringToObject(wifi, "password", PASSWORD_NOT_CHANGED);
	}
	char* str = cJSON_Print(copy);
	cJSON_Delete(copy);
	return str;
}

//static void set_str_value(cJSON* node, char* str) {
//	if (!node) return;
//	if (node->valuestring) free(node->valuestring);
//	node->valuestring = malloc(strlen(str)+1);
//	strcpy(node->valuestring, str);
//}

/*
 * deserialises json string and stores as a new config.
 * it also checks password field if it that didn't change - it leaves the old value.
 */
int storage_set_config_str(const char* configStr) {
	ESP_LOGD(TAG, "set config");

//	char* c= storage_get_config_str();
//	ESP_LOGD(TAG, "current config: %s", c);
//	free(c);

	cJSON *config = cJSON_Parse(configStr);
	if (config) {
		if (_config) {
			cJSON* wifi = cJSON_GetObjectItem(config, "wifi");
			cJSON* passwordNode = cJSON_GetObjectItem(wifi, "password");
			if (passwordNode && strcmp(passwordNode->valuestring, PASSWORD_NOT_CHANGED) == 0) {
				cJSON* _wifi = cJSON_GetObjectItem(_config, "wifi");
				cJSON* _passwordNode = cJSON_GetObjectItem(_wifi, "password");
				ESP_LOGD(TAG,"detected '%s' token, we need to keep old password (%s)", PASSWORD_NOT_CHANGED, _passwordNode->valuestring);
				if (_passwordNode) {
					cJSON_DeleteItemFromObject(wifi, "password");
					cJSON_AddStringToObject(wifi, "password", _passwordNode->valuestring);
				}
			}
		}
		storage_set_config(config);
		return ESP_OK;
	} else {
		ESP_LOGE(TAG, "malformed config, ignore");
		return ESP_FAIL;
	}
}



static char* default_config() {
	int len = default_config_json_end-default_config_json_start;
	char *str = (char *) malloc(len + 1);
	memcpy(str, default_config_json_start, len);
	str[len] = 0;
	return str;
}

static void storage_init_config() {
	char* str = NULL;
	ESP_LOGD(TAG, "get config");

	int err;
	if ((err = storage_get_str("config", &str)) != ESP_OK) {
		if (str) free(str);
		if (err == ESP_ERR_NVS_NOT_FOUND) {
			ESP_LOGW(TAG,"config does not exist, create default");
		} else {
			ESP_LOGE(TAG,"config corrupted, replace with default");
		}
	} else {
		_config = cJSON_Parse(str);
		if (!_config) {
			ESP_LOGE(TAG,"config is not a proper json, replace with default\n%s", str);
		} else {
			//TODO here we should "merge" it with defaults - in case of new firmware
			ESP_LOGI(TAG,"config\n%s",str);
		}
		free(str);
	}

	if (!_config) {
		str = default_config();
		_config = cJSON_Parse(str);
		if (!_config) {
			ESP_LOGE(TAG,"default config is not a proper json\n%s", str);
			abort();
		} else {
			ESP_LOGI(TAG,"config\n%s",str);
		}
		storage_set_config_str(str);
		free(str);
	}
}

void storage_init() {
	nvs_flash_init();
	storage_init_config();
}

