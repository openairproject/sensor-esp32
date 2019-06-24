/*
 * ota.c
 *
 *  Created on: Sep 10, 2017
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

#include "ota_int.h"

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

#include "oap_common.h"
#include "oap_debug.h"
#include "bootwifi.h"

#include "esp_request.h"
#include "mbedtls/sha256.h"

#define TAG "ota"


//extern const uint8_t _root_ca_pem_start[] asm("_binary_comodo_ca_pem_start");
//extern const uint8_t _root_ca_pem_end[]   asm("_binary_comodo_ca_pem_end");
extern const uint8_t _root_ca_pem_start[] asm("_binary_lets_encrypt_x3_cross_signed_pem_start");
extern const uint8_t _root_ca_pem_end[]   asm("_binary_lets_encrypt_x3_cross_signed_pem_end");

void sha_to_hexstr(unsigned char hash[32], unsigned char hex[64]) {
	for (int i = 0; i < 32; i++) {
		snprintf((char*)hex + i*2,3,"%02x",hash[i]);
	}
}

char* sha_to_hex(unsigned char hash[32]) {
	char* hex = malloc(2*32+1);
	hex[2*32] = 0;
	for (int i = 0; i < 32; i++) {
		snprintf(hex + i*2,3,"%02x",hash[i]);
	}
	return hex;
}

typedef struct {
	int found;
	esp_err_t err;
	ota_info_t binary;
} ost_status_result_t;

static int is_white_char(int ch) {
	return ch == '\n' || ch == '\r' || ch == '\t';
}

void reset_to_factory_partition() {
	const esp_partition_t *factory = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
	if (factory) {
		esp_ota_set_boot_partition(factory);
		oap_reboot("reset to factory");
	} else {
		ESP_LOGE(TAG, "no factory partition?");
	}
}

esp_err_t parse_ota_info(ota_info_t* ota_info, char* line, int len) {
	if (len <= 0) return OAP_OTA_ERR_EMPTY_RESPONSE;
	char* ver = NULL; char* file = NULL; char* sha = NULL;
	int i = -1; int start = 0;
	do {
		i++;
		if (line[i] == '|' || line[i] == 0 || is_white_char(line[i])) {
			if (ver == NULL) {
				ver = str_make(line+start, i-start);
			} else if (file == NULL) {
				file = str_make(line+start, i-start);
			} else if (sha == NULL) {
				sha = str_make(line+start, i-start);
				break;
			}
			start = i+1;
		}
	} while(line[i] && i < len);

	if (!ver) {
		ESP_LOGW(TAG, "malformed status (no version)");
		goto fail;
	}
	if (!file) {
		ESP_LOGW(TAG, "malformed status (no file)");
		goto fail;
	}
	if (!sha) {
		ESP_LOGW(TAG, "malformed status (no sha)");
		goto fail;
	}

	if (oap_version_parse(ver, &ota_info->ver) != ESP_OK) {
		ESP_LOGW(TAG, "malformed status (invalid version: '%s')", ver);
		goto fail;
	}
	ota_info->file = file;
	ota_info->sha = sha;
	free(ver);
	return ESP_OK;

	fail:
		if (ver) free(ver);
		if (file) free(file);
		if (sha) free(sha);
		return OAP_OTA_ERR_MALFORMED_INFO;
}

static int get_ota_status_callback(request_t *req, char *data, int len)
{
	ESP_LOGD(TAG,"index:\n%s", data);
	ost_status_result_t* result = req->meta;
	if (req->response->status_code == 200 && !result->found) {
		result->found = 1;
		result->err = parse_ota_info(&result->binary, data, len);
		ESP_LOGD(TAG, "parse ota info line... [0x%x]", result->err);
	}
	return 0;
}

void free_ota_info(ota_info_t* ota_info) {
	if (ota_info) {
		if (ota_info->file) free(ota_info->file);
		if (ota_info->sha) free(ota_info->sha);
		ota_info->file=NULL;
		ota_info->sha=NULL;
	}
}

esp_err_t fetch_last_ota_info(ota_config_t* ota_config, ota_info_t* ota_info)
{
	ESP_LOGI(TAG, "fetch ota info from %s", ota_config->index_uri);
    request_t* req = req_new(ota_config->index_uri);
    if (!req) {
    	return OAP_OTA_ERR_REQUEST_FAILED;
    }

//	ESP_LOGI(TAG, "REQ.HOST:%s", (char*)req_list_get_key(req->opt, "host")->value);
//	ESP_LOGI(TAG, "REQ.PATH:%s", (char*)req_list_get_key(req->opt, "path")->value);
//	ESP_LOGI(TAG, "REQ.CLIENT_CERT:%p", req->client_cert);
//	ESP_LOGI(TAG, "REQ.CLIENT_KEY:%p", req->client_key);

    req->ca_cert = req_parse_x509_crt((unsigned char*)_root_ca_pem_start, _root_ca_pem_end-_root_ca_pem_start);

    ost_status_result_t result;
    memset(&result, 0, sizeof(ost_status_result_t));
    req->meta = &result;

    req_setopt(req, REQ_SET_HEADER, HTTP_HEADER_CONNECTION_CLOSE);
    req_setopt(req, REQ_FUNC_DOWNLOAD_CB, get_ota_status_callback);
    req_set_user_agent(req);
    
    int status = req_perform(req);
    req_clean(req);

    if (status != 200) {
        ESP_LOGW(TAG, "error response code=%d", status);
    	return OAP_OTA_ERR_REQUEST_FAILED;
    }

    if (result.found) {
    	if (result.err == ESP_OK) {
    		memcpy(ota_info, &result.binary, sizeof(ota_info_t));
    	}
    	return result.err;
    } else {
    	return OAP_OTA_ERR_EMPTY_RESPONSE;
    }
}


typedef struct {
	mbedtls_sha256_context* sha_context;
	esp_ota_handle_t ota_handle;
} ota_download_callback_meta;

static int download_ota_binary_callback(request_t *req, char *data, size_t len)
{
	ota_download_callback_meta* meta = req->meta;
    mbedtls_sha256_update(meta->sha_context, (unsigned char *)data, len);
    if (meta->ota_handle) {
    	return esp_ota_write(meta->ota_handle, (const void *)data, len);
    } else {
    	return 0;
    }
}

static esp_err_t download_ota_binary(ota_config_t* ota_config, ota_info_t* ota_info, esp_ota_handle_t ota_handle)
{
    char file_uri[300];
    sprintf(file_uri, "%s%s", ota_config->bin_uri_prefix, ota_info->file);

    ESP_LOGI(TAG, "download ota binary from %s", file_uri);

    request_t* req = req_new(file_uri);
    if (!req) {
    	return OAP_OTA_ERR_REQUEST_FAILED;
    }
    req->ca_cert = req_parse_x509_crt((unsigned char*)_root_ca_pem_start, _root_ca_pem_end-_root_ca_pem_start);

    req_setopt(req, REQ_SET_HEADER, "Connection: close");
    req_setopt(req, REQ_FUNC_DOWNLOAD_CB, download_ota_binary_callback);
    req_set_user_agent(req);

    mbedtls_sha256_context sha_context;
    mbedtls_sha256_init(&sha_context);
    mbedtls_sha256_starts(&sha_context,0);

    ota_download_callback_meta meta = {
    	.sha_context = &sha_context,
		.ota_handle = ota_handle
    };

    req->meta = &meta;

    int status = req_perform(req);
    ESP_LOGI(TAG, "status=%d", status);

    esp_err_t ret;
    if (status == 200) {
    	ESP_LOGI(TAG, "ota file downloaded");
		ret = ESP_OK;
    	unsigned char hash[32];
    	mbedtls_sha256_finish(&sha_context, hash);
    	char* hex = sha_to_hex(hash);
		ESP_LOGI(TAG, "file sha256=%s", hex);
		if (strcmp(hex, ota_info->sha) != 0) {
			ESP_LOGE(TAG, "invalid sha (expected: %s)", ota_info->sha);
			ret = OAP_OTA_ERR_SHA_MISMATCH;
		}
		free(hex);
    } else {
    	ESP_LOGW(TAG, "error response code=%d", status);
    	ret = OAP_OTA_ERR_REQUEST_FAILED;
    }

    mbedtls_sha256_free(&sha_context);
	req_clean(req);
    return ret;
}

esp_err_t is_ota_update_available(ota_config_t* ota_config, ota_info_t* ota_info) {
	esp_err_t err = fetch_last_ota_info(ota_config, ota_info);
	if (err == ESP_OK) {
		unsigned long remote_ver = oap_version_num(ota_info->ver);
		if (remote_ver <= ota_config->min_version) {
			ESP_LOGD(TAG, "remote ver: %lu <= min ver: %lu", remote_ver, ota_config->min_version);
			err = OAP_OTA_ERR_NO_UPDATES;
		} else {
			ESP_LOGD(TAG, "new update found (%lu)", remote_ver);
		}
	} else {
		ESP_LOGD(TAG, "fetch_last_ota_info failed [0x%x]", err);
	}
	return err;
}

esp_err_t check_ota(ota_config_t* ota_config) {


	const esp_partition_t* running_partition = esp_ota_get_running_partition();
	ESP_LOGI(TAG, "running partition = %s", running_partition->label);
	if (strcmp("factory", running_partition->label) != 0) {
		ESP_LOGW(TAG, "OTA partition! Reset to factory for a DEV mode!");
	}
	if (!ota_config->update_partition) {
		ota_config->update_partition = esp_ota_get_next_update_partition(NULL);
	}
	if (ota_config->update_partition == NULL) {
		ESP_LOGE(TAG, "no suitable OTA partition found");
		return ESP_FAIL;
	}
	ESP_LOGI(TAG, "update partition = %s", ota_config->update_partition->label);
	esp_err_t err;
	esp_ota_handle_t update_handle = 0;
	ota_info_t ota_info = {
		.sha =NULL,
		.file=NULL
	};

	while (1) {
		if ((err = wifi_connected_wait_for(60000)) != ESP_OK) {
			goto go_sleep;
		}

		ESP_LOGD(TAG, "Check for OTA updates...");
		log_task_stack(TAG);

		if ((err = is_ota_update_available(ota_config, &ota_info)) != ESP_OK) goto go_sleep;

		char* verstr = oap_version_format(ota_info.ver);
		ESP_LOGW(TAG,"NEW FIRMWARE AVAILABLE: %s", verstr);
		free(verstr);

		ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
				ota_config->update_partition->subtype, ota_config->update_partition->address);

		err = esp_ota_begin(ota_config->update_partition, OTA_SIZE_UNKNOWN, &update_handle);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "esp_ota_begin failed [0x%x]", err);
			goto fail;
		}
		ESP_LOGI(TAG, "esp_ota_begin succeeded");

		//download
		if ((err = download_ota_binary(ota_config, &ota_info, update_handle)) != ESP_OK) {
			ESP_LOGE(TAG, "download_ota_binary failed [0x%x]", err);
			goto fail;
		}

		if ((err=esp_ota_end(update_handle)) != ESP_OK) {
		   ESP_LOGE(TAG, "esp_ota_end failed [0x%x]", err);
		   goto fail;
		}

		if (ota_config->commit_and_reboot) {
			if ((err = esp_ota_set_boot_partition(ota_config->update_partition)) != ESP_OK) {
			   ESP_LOGE(TAG, "esp_ota_set_boot_partition failed [0x%x]", err);
			   goto fail;
			}

			ESP_LOGW(TAG, "OTA applied. Prepare to restart system!");
			oap_reboot("OTA update");
			return ESP_OK;
		} else {
			ESP_LOGW(TAG, "OTA downloaded but configured to be ignored");
			goto go_sleep;
		}

		fail:
		   ESP_LOGE(TAG,"Interrupt OTA");

		go_sleep:
		free_ota_info(&ota_info);

		if (ota_config->interval <= 0) {
			break;
		} else {
			ESP_LOGD(TAG, "sleep for %d sec", ota_config->interval/1000);
			delay(ota_config->interval);
		}
	}
	return err;
}

static void check_ota_task(ota_config_t* ota_config) {
	delay(1000);
	check_ota(ota_config);
	vTaskDelete(NULL);
}

static ota_config_t ota_config;

void start_ota_task(cJSON* user_ota_config) {

	if (OAP_OTA_ENABLED) {
		cJSON* ota_interval;
		if (!user_ota_config ||
				!(ota_interval = cJSON_GetObjectItem(user_ota_config, "interval")) || ota_interval->valueint < 0) {
			//disable for -1, for 0 it will run once at startup
			ESP_LOGI(TAG, "OTA disabled");
		} else {
			ota_config.bin_uri_prefix = OAP_OTA_BIN_URI_PREFIX;
			ota_config.index_uri = OAP_OTA_INDEX_URI;
			ota_config.min_version = oap_version_num(oap_version());
			ota_config.commit_and_reboot = 1;
			ota_config.update_partition = NULL;
			ota_config.interval = 1000 * ota_interval->valueint;
			xTaskCreate((TaskFunction_t)check_ota_task, "check_ota_task", 1024*6, &ota_config, DEFAULT_TASK_PRIORITY, NULL);
		}
	} else {
		ESP_LOGI(TAG, "OTA not available");
	}
}
