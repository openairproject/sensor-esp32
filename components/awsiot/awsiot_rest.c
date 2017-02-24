/*
 * aws_iot_rest.c
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

#include "awsiot_rest.h"
#include "ssl_client.h"
#include "oap_common.h"

//#define WEB_SERVER "a32on3oilq3poc.iot.eu-west-1.amazonaws.com"
//#define WEB_PORT "8443"
//#define WEB_URL "/things/pm_wro_2/shadow"

static const char *TAG = "awsiot";

extern const uint8_t verisign_root_ca_pem_start[] asm("_binary_verisign_root_ca_pem_start");
extern const uint8_t verisign_root_ca_pem_end[]   asm("_binary_verisign_root_ca_pem_end");


esp_err_t awsiot_update_shadow(awsiot_config_t awsiot_config, char* body) {
	sslclient_context ssl_client = {};
	ssl_init(&ssl_client);
	int ret = ESP_OK;

	ESP_LOGD(TAG, "connecting to %s:%d", awsiot_config.endpoint,awsiot_config.port);
	if ((ssl_client.socket = open_socket(awsiot_config.endpoint,awsiot_config.port,5,0)) < 0) {
		return ssl_client.socket;
	} else {
		ESP_LOGD(TAG, "connected");
	}

	char* rootCA = str_make((void*)verisign_root_ca_pem_start, verisign_root_ca_pem_end-verisign_root_ca_pem_start);
	if (start_ssl_client(&ssl_client, (unsigned char*)rootCA, (unsigned char*)awsiot_config.cert, (unsigned char*)awsiot_config.pkey) > 0) {
		free(rootCA);
		char* request = malloc(strlen(body) + 250);

		sprintf(request, "POST /things/%s/shadow HTTP/1.1\n"
		    "Host: %s\n"
			"Content-Type: application/json\n"
			"Connection: close\n"
			"Content-Length: %d\n"
		    "\r\n%s", awsiot_config.thingName, awsiot_config.endpoint, strlen(body), body);

		ESP_LOGD(TAG, "%s", request);

		send_ssl_data(&ssl_client, (uint8_t *)request, strlen(request));
		free(request);

		int len;
		//TODO parse at least status code (would be nice to get json body) too
		unsigned char buf[1024];
		do {
			len = get_ssl_receive(&ssl_client, buf, 1024);
			if (len == MBEDTLS_ERR_SSL_WANT_READ || len == MBEDTLS_ERR_SSL_WANT_WRITE) {
				continue;
			} else if (len == -0x4C) {
				ESP_LOGD(TAG, "timeout");
				break;
			} else if (len <= 0) {
				ret = len;
				break;
			}
			for (int i =0; i < len ; i++) {
				putchar(buf[i]);
			}
		} while (1);
	} else {
		free(rootCA);
		ret = ESP_FAIL;
	}
	stop_ssl_socket(&ssl_client);

	ESP_LOGI(TAG, "ssl request done %d", ret);

	return ret;
}
