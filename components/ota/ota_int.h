/*
 * ota_int.h
 *
 *  Created on: Sep 14, 2017
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

#ifndef COMPONENTS_OTA_OTA_INT_H_
#define COMPONENTS_OTA_OTA_INT_H_


#include "ota.h"
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "oap_version.h"

#define OAP_OTA_ERR_REQUEST_FAILED 		0x1001
#define OAP_OTA_ERR_NO_UPDATES 			0x1002
#define OAP_OTA_ERR_EMPTY_RESPONSE		0x1003
#define OAP_OTA_ERR_MALFORMED_INFO		0x1004
#define OAP_OTA_ERR_SHA_MISMATCH		0x1005

typedef struct {
	char* bin_uri_prefix;
	char* index_uri;
	unsigned int interval; // for <=0 it checks only once
	int commit_and_reboot;
	esp_partition_t *update_partition;
	unsigned long min_version;
} ota_config_t;

typedef struct {
	char* sha;
	char* file;
	oap_version_t ver;
} ota_info_t;

esp_err_t is_ota_available(ota_config_t* ota_config, ota_info_t* ota_info);
esp_err_t fetch_last_ota_info(ota_config_t* ota_config, ota_info_t* ota_info);
esp_err_t download_ota_binary(ota_config_t* ota_config, ota_info_t* ota_info, esp_partition_t *update_partition);
esp_err_t parse_ota_info(ota_info_t* ota_info, char* line, int len);
esp_err_t check_ota(ota_config_t* ota_config);


#endif /* COMPONENTS_OTA_OTA_INT_H_ */
