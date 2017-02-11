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

#include <esp_log.h>
#include <esp_err.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>

static char* NAMESPACE = "OAP";
static char* TAG = "storage";

int storage_get_blob(const char* key, void* out_value, size_t length) {
	nvs_handle handle;
	esp_err_t err;
	err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "nvs_open: %x", err);
		return -1;
	}
	err = nvs_get_blob(handle, key, out_value, &length);
	if (err != ESP_OK) {
		ESP_LOGD(TAG, "No connection record found (%d).", err);
		nvs_close(handle);
		return -1;
	}
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "nvs_open: %x", err);
		nvs_close(handle);
		return -1;
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

