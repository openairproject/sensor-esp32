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
#include <math.h>

extern const uint8_t default_config_json_start[] asm("_binary_default_config_json_start");
extern const uint8_t default_config_json_end[] asm("_binary_default_config_json_end");

static char* NAMESPACE = "OAP";
static char* TAG = "storage";
static char* PASSWORD_NOT_CHANGED = "<not-changed>";

static const size_t MAX_NVS_VALUE_SIZE = 32 * (126 / 2 - 1);

static cJSON* _config;

static nvs_handle storage_open(nvs_open_mode open_mode) {
	nvs_handle handle;
	ESP_ERROR_CHECK(nvs_open(NAMESPACE, open_mode, &handle));
	return handle;
}

void storage_clean() {
	ESP_ERROR_CHECK(nvs_flash_init());
	nvs_handle handle = storage_open(NVS_READWRITE);
	ESP_ERROR_CHECK(nvs_erase_all(handle));	//TODO this fails is wifi was initialised before?
	ESP_ERROR_CHECK(nvs_commit(handle));
	nvs_close(handle);
	_config = NULL;
}

void storage_erase_blob(const char* key) {
	nvs_handle handle = storage_open(NVS_READWRITE);
	esp_err_t err = nvs_erase_key(handle, key);
	if (err != ESP_ERR_NVS_NOT_FOUND) {
		ESP_ERROR_CHECK(err);
		ESP_ERROR_CHECK(nvs_commit(handle));
	}
	nvs_close(handle);
}

esp_err_t storage_get_blob(const char* key, void** out_value, size_t* length) {
	nvs_handle handle = storage_open(NVS_READWRITE);
	esp_err_t err;
	size_t _length;
	err = nvs_get_blob(handle, key, 0, &_length);
	if (err == ESP_ERR_NVS_NOT_FOUND) {
		nvs_close(handle);
		return err;
	}
	ESP_ERROR_CHECK(err);

	if (length != NULL) {
		//fill optional length param
		memcpy(length, &_length, sizeof(size_t));
	}

	*out_value = malloc(_length);
	ESP_ERROR_CHECK(nvs_get_blob(handle, key, *out_value, &_length));
	nvs_close(handle);
	return ESP_OK;
}

/*
esp_err_t storage_get_str(const char* key, char** out_value) {
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
		ESP_LOGD(TAG, "nvs_get_str (%x).", err);
		nvs_close(handle);
		return err;
	}
	*out_value = malloc(length);

	err = nvs_get_str(handle, key, *out_value, &length);
	if (err != ESP_OK) {
		ESP_LOGD(TAG, "nvs_get_str (%x).", err);
		nvs_close(handle);
		return err;
	}
	nvs_close(handle);
	return ESP_OK;
}*/

void storage_set_blob(const char* key, void* value, size_t length) {
	nvs_handle handle = storage_open(NVS_READWRITE);
	ESP_ERROR_CHECK(nvs_set_blob(handle, key, value, length));
	ESP_ERROR_CHECK(nvs_commit(handle));
	nvs_close(handle);
}

/*
void storage_put_str(const char* key, char* value) {
	nvs_handle handle;
	ESP_ERROR_CHECK(nvs_open(NAMESPACE, NVS_READWRITE, &handle));
	ESP_LOGD(TAG, "store string '%s'=%d bytes", key, strlen(value));
	ESP_ERROR_CHECK(nvs_set_str(handle, key, value));
	ESP_ERROR_CHECK(nvs_commit(handle));
	nvs_close(handle);
}*/

//----------- big blob --

static esp_err_t storage_get_bigblob_size(nvs_handle handle, const char* key, size_t* length) {
	char* desc = malloc(strlen(key) + 3);
	strcpy(desc, key);
	strcat(desc, ".#");
	esp_err_t err = nvs_get_i32(handle, desc, (int32_t*)length);
	free(desc);
	return err;
}

static esp_err_t storage_set_bigblob_size(nvs_handle handle, const char* key, size_t length) {
	char* desc = malloc(strlen(key) + 3);
	strcpy(desc, key);
	strcat(desc, ".#");
	esp_err_t err = nvs_set_i32(handle, desc, length);
	free(desc);
	return err;
}

esp_err_t storage_get_bigblob(const char* key, void** out_value, size_t* length) {
	nvs_handle handle = storage_open(NVS_READWRITE);
	esp_err_t err;
	//read key.desc to find length and #parts
	size_t _length;
	err = storage_get_bigblob_size(handle, key, &_length);

	if (err != ESP_OK) {
		ESP_LOGD(TAG, "No entry found (%x).", err);
		nvs_close(handle);
		return err;
	} else {
		ESP_LOGD(TAG, "reading %u bytes", _length);
		if (length != NULL) {
			//fill optional length param
			memcpy(length, &_length, sizeof(size_t));
		}
	}

	*out_value = malloc(_length);
	int p = 0;
	char* part = malloc(strlen(key) + 4);
	size_t start = 0, end = 0;
	err = ESP_OK;

	while (err == ESP_OK && end < _length) {
		sprintf(part, "%s.%x", key, p);
		start = p * MAX_NVS_VALUE_SIZE;
		end = start + MAX_NVS_VALUE_SIZE;
		if (end > _length) end = _length;
		ESP_LOGD(TAG, "read %s: %d-%d", part, start, end);
		size_t part_len = end-start;
		err = nvs_get_blob(handle, part, *out_value+start, &part_len);
		p++;
	}
	free(part);
	nvs_close(handle);
	return err;
}

void storage_set_bigblob(const char* key, void* value, size_t length) {
	nvs_handle handle = storage_open(NVS_READWRITE);
	ESP_LOGD(TAG, "set_bigblob '%s'= %d bytes", key, length);

	size_t old_length;
	uint8_t old_parts = 0;
	esp_err_t err = storage_get_bigblob_size(handle, key, &old_length);
	if (err == ESP_OK) {
		old_parts = ceil(old_length / (double)MAX_NVS_VALUE_SIZE);
		ESP_LOGD(TAG, "old entry size=%d (%d parts)", old_length, old_parts);
	} else if (err == ESP_ERR_NVS_NOT_FOUND) {
		ESP_LOGD(TAG, "new entry");
	} else {
	  ESP_ERROR_CHECK(err);
	}

	//write new one
	uint8_t p = 0;
	size_t start = 0, end = 0;
	char* part = malloc(strlen(key) + 4);

	while (end < length) {
		start = p*MAX_NVS_VALUE_SIZE;
		end =  start + MAX_NVS_VALUE_SIZE;
		if (end > length) end = length;
		sprintf(part, "%s.%x", key, p);
		ESP_LOGD(TAG, "store part '%s': %d-%d", part, start, end);
		ESP_ERROR_CHECK(nvs_set_blob(handle, part, value+start, end-start));
		nvs_commit(handle);
		p++;
	}

	while (p < old_parts) {
		sprintf(part, "%s.%x", key, p);
		ESP_LOGD(TAG, "remove part '%s'", part);
		err = nvs_erase_key(handle, part);
		nvs_commit(handle);
		if (err != ESP_ERR_NVS_NOT_FOUND) {
			ESP_ERROR_CHECK(err);
		}
		p++;
	}
	free(part);

	ESP_ERROR_CHECK(storage_set_bigblob_size(handle, key, length));
	nvs_commit(handle);
	nvs_close(handle);
}

//----------- config --------------

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

static void storage_store_config(cJSON *config) {
	if (_config) cJSON_Delete(_config);
	_config = cJSON_Duplicate(config, 1);
	char* json = cJSON_Print(_config);
	storage_set_bigblob("config", json, strlen(json)+1);
	free(json);
	storage_erase_blob("config"); //remove old entry
}

static void mask_sensitive_fields(cJSON* config) {
	cJSON* wifi = cJSON_GetObjectItem(config, "wifi");
	//replace existing password (security)
	if (wifi && cJSON_GetObjectItem(wifi, "password")) {
		cJSON_DeleteItemFromObject(wifi, "password");
		cJSON_AddStringToObject(wifi, "password", PASSWORD_NOT_CHANGED);
	}
}

cJSON* storage_get_config_to_update() {
	cJSON* copy = cJSON_Duplicate(_config, 1);
	mask_sensitive_fields(copy);
	return copy;
}

static void unmask_sensitive_fields(cJSON* new_config, cJSON* old_config) {
	if (old_config) {
		cJSON* wifi = cJSON_GetObjectItem(new_config, "wifi");
		if (wifi) {
			cJSON* passwordNode = cJSON_GetObjectItem(wifi, "password");
			if (passwordNode && strcmp(passwordNode->valuestring, PASSWORD_NOT_CHANGED) == 0) {
				cJSON* _wifi = cJSON_GetObjectItem(old_config, "wifi");
				if (_wifi) {
					cJSON* _passwordNode = cJSON_GetObjectItem(_wifi, "password");
					ESP_LOGD(TAG,"detected '%s' token, we need to keep old password", PASSWORD_NOT_CHANGED);
					if (_passwordNode) {
						cJSON_DeleteItemFromObject(wifi, "password");
						cJSON_AddStringToObject(wifi, "password", _passwordNode->valuestring);
					}
				}
			}
		}
	}
}

void storage_update_config(cJSON* config) {
	ESP_LOGD(TAG, "update config");
	if (!config) return;
	unmask_sensitive_fields(config, _config);
	storage_store_config(config);
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
	if ((err = storage_get_bigblob("config", (void**)&str, NULL)) == ESP_ERR_NVS_NOT_FOUND) {
		err = storage_get_blob("config", (void**)&str, NULL);	//backward comp, config used to be stored as single string
	}

	cJSON* stored = NULL;

	if (err != ESP_OK) {
		if (str) free(str);
		if (err == ESP_ERR_NVS_NOT_FOUND) {
			ESP_LOGW(TAG,"config does not exist, create default");
		} else {
			ESP_LOGE(TAG,"config corrupted, replace with default");
		}
	} else {
		stored = cJSON_Parse(str);
		if (!stored) {
			ESP_LOGE(TAG,"config is not a proper json, replace with default\n%s", str);
		} else {
			//TODO here we should "merge" it with defaults - in case of new firmware
			ESP_LOGI(TAG,"config\n%s",str);
		}
		free(str);
	}

	if (stored) {
		_config = stored;
	} else {
		str = default_config();
		cJSON* def_config = cJSON_Parse(str);
		if (!def_config) {
			ESP_LOGE(TAG,"default config is not a proper json\n%s", str);
			abort();
		} else {
			ESP_LOGD(TAG,"default config\n%s",str);
		}
		free(str);
		storage_update_config(def_config);
		cJSON_Delete(def_config);
	}
}

void storage_init() {
	ESP_ERROR_CHECK(nvs_flash_init());
	storage_init_config();
}

