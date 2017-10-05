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
//#include "ssl_client.h"
#include "oap_common.h"
#include "oap_debug.h"
#include "esp_request.h"

//#define WEB_SERVER "a32on3oilq3poc.iot.eu-west-1.amazonaws.com"
//#define WEB_PORT "8443"
//#define WEB_URL "/things/pm_wro_2/shadow"

static const char *TAG = "awsiot";

extern const uint8_t verisign_root_ca_pem_start[] asm("_binary_verisign_root_ca_pem_start");
extern const uint8_t verisign_root_ca_pem_end[]   asm("_binary_verisign_root_ca_pem_end");

static int download_callback(request_t *req, char *data, int len) {
	ESP_LOGI(TAG, "response:%s", data);
	return 0;
}

esp_err_t awsiot_update_shadow(awsiot_config_t* awsiot_config, char* body) {
	char uri[100];
	sprintf(uri, "https://%s:%d", awsiot_config->endpoint, awsiot_config->port);
//	char host_header[100];
//	sprintf(host_header, "Host: %s", awsiot_config->endpoint);

	request_t* req = req_new(uri);
	if (!req) {
		return ESP_FAIL;
	}

	req->ca_cert = req_parse_x509_crt((unsigned char*)verisign_root_ca_pem_start, verisign_root_ca_pem_end-verisign_root_ca_pem_start);
	if (!req->ca_cert) {
		ESP_LOGW(TAG, "Invalid CA cert");
		return ESP_FAIL;
	}

	req->client_cert = req_parse_x509_crt((unsigned char*)awsiot_config->cert, strlen(awsiot_config->cert)+1);
	if (!req->client_cert) {
		ESP_LOGW(TAG, "Invalid client cert");
		return ESP_FAIL;
	}
	req->client_key = req_parse_pkey((unsigned char*)awsiot_config->pkey, strlen(awsiot_config->pkey)+1);

	if (!req->client_key) {
		ESP_LOGW(TAG, "Invalid client key");
		return ESP_FAIL;
	}

	char path[100];
	sprintf(path, "/things/%s/shadow", awsiot_config->thingName);


	req_setopt(req, REQ_SET_METHOD, "POST");
	req_setopt(req, REQ_SET_PATH, path);
	//req_setopt(req, REQ_SET_HEADER, host_header);
	req_setopt(req, REQ_SET_HEADER, "Content-Type: application/json");
	req_setopt(req, REQ_SET_HEADER, "Connection: close");
	req_setopt(req, REQ_SET_DATAFIELDS, body);

	req_setopt(req, REQ_FUNC_DOWNLOAD_CB, download_callback);

	int status = req_perform(req);
	req_clean_incl_certs(req);

	if (status != 200) {
		ESP_LOGW(TAG, "Invalid response code: %d", status);
		return ESP_FAIL;
	} else {
		return ESP_OK;
	}
}


/*
#define RESPONSE_BUF_SIZE 1024

esp_err_t awsiot_update_shadow_old(awsiot_config_t awsiot_config, char* body) {
	//heap_log* hl = heap_log_take(NULL, "start");
	sslclient_context ssl_client = {.0};
	ssl_init(&ssl_client);
	int ret = ESP_OK;

	//heap_log_take(hl, "after ssl_init");
	ESP_LOGD(TAG, "connecting to %s:%d", awsiot_config.endpoint,awsiot_config.port);
	if ((ssl_client.socket = open_socket(awsiot_config.endpoint,awsiot_config.port,1,0)) < 0) {
		return ssl_client.socket;
	} else {
		ESP_LOGD(TAG, "connected");
	}
	//heap_log_take(hl, "connected");

	char* rootCA = str_make((void*)verisign_root_ca_pem_start, verisign_root_ca_pem_end-verisign_root_ca_pem_start);
	if (start_ssl_client(&ssl_client, (unsigned char*)rootCA, (unsigned char*)awsiot_config.cert, (unsigned char*)awsiot_config.pkey, awsiot_config.endpoint) == ESP_OK) {
		free(rootCA);
		//heap_log_take(hl, "start_ssl_client");
		char* request = malloc(strlen(body) + 250);

		sprintf(request, "POST /things/%s/shadow HTTP/1.1\n"
		    "Host: %s\n"
			"Content-Type: application/json\n"
			"Connection: close\n"
			"Content-Length: %d\n"
		    "\r\n%s", awsiot_config.thingName, awsiot_config.endpoint, strlen(body), body);

		ESP_LOGD(TAG, "%s (%d bytes)", request, strlen(request));

		//heap_log_take(hl, "built request");

		send_ssl_data(&ssl_client, (uint8_t *)request, strlen(request));

		//heap_log_take(hl, "send_ssl_data");

		free(request);

		//heap_log_take(hl, "free request");

		int len;
		//TODO parse at least status code (would be nice to get json body) too
		unsigned char* buf = malloc(RESPONSE_BUF_SIZE);
		do {
			len = get_ssl_receive(&ssl_client, buf, RESPONSE_BUF_SIZE);
			if (len == MBEDTLS_ERR_SSL_WANT_READ || len == MBEDTLS_ERR_SSL_WANT_WRITE) {
				continue;
			} else if (len == -0x4C) {
				ESP_LOGD(TAG, "timeout");
				break;
			} else if (len <= 0) {
				ret = len;
				break;
			}
			for (int i=0; i < len ; i++) {
				putchar(buf[i]);
			}
		} while (1);
		free(buf);
	} else {
		free(rootCA);
		ret = ESP_FAIL;
	}
	//heap_log_take(hl, "request done");
	stop_ssl_socket(&ssl_client);
	//heap_log_take(hl, "stop_ssl_socket");
	//heap_log_free(hl);
	ESP_LOGI(TAG, "ssl request done %d", ret);

	return ret;
}*/
