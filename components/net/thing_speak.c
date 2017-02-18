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
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "thing_speak.h"
#include "oap_common.h"
#include "oap_storage.h"

#define OAP_THING_SPEAK_HOST "api.thingspeak.com"
#define OAP_THING_SPEAK_PORT 80
#define OAP_THING_SPEAK_PATH "/update"

static const char *TAG = "thingspk";

static char* apikey = NULL;
static QueueHandle_t input_queue;

static int post_data(oap_meas meas) {
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int sck, r;
    char recv_buf[64];

    int err = getaddrinfo(OAP_THING_SPEAK_HOST, "80", &hints, &res);

	if(err != 0 || res == NULL) {
		ESP_LOGW(TAG, "DNS lookup failed err=%d res=%p", err, res);
		return 0;
	}

	/* Code to print the resolved IP.
	   Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
	addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
	ESP_LOGD(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

	sck = socket(res->ai_family, res->ai_socktype, 0);
	if(sck < 0) {
		ESP_LOGW(TAG, "failed to allocate socket.");
		freeaddrinfo(res);
		return 0;
	}

	err = connect(sck, res->ai_addr, res->ai_addrlen);
	if(err != 0) {
		ESP_LOGW(TAG, "socket connect failed errno=%d", err);
		close(sck);
		freeaddrinfo(res);
		return 0;
	}

	freeaddrinfo(res);

	char payload[200];
	sprintf(payload, "key=%s&field1=%d&field2=%d&field3=%d&field4=%.2f&field5=%.2f&field6=%.2f", apikey,
			meas.pm.pm1_0,
			meas.pm.pm2_5,
			meas.pm.pm10,
			meas.env.temp,
			meas.env.pressure,
			meas.env.humidity);

	char request[512];

	sprintf(request, "POST %s HTTP/1.1\n"
	    "Host: %s\n"
		"Content-Type: application/x-www-form-urlencoded\n"
		"Content-Length: %d\n"
	    "\r\n%s", OAP_THING_SPEAK_PATH, OAP_THING_SPEAK_HOST, strlen(payload), payload);

	ESP_LOGD(TAG, "request:\n%s", request);

	if (write(sck, request, strlen(request)) < 0) {
		ESP_LOGW(TAG, "socket send failed");
		close(sck);
		return 0;
	}
	ESP_LOGD(TAG, "socket send success");

	int response_size = 0;
	do {
		bzero(recv_buf, sizeof(recv_buf));
		r = read(sck, recv_buf, sizeof(recv_buf)-1);
		response_size+=r;
		for(int i = 0; i < r; i++) {
			putchar(recv_buf[i]);
		}
	} while(r > 0);

	ESP_LOGD(TAG, "... done reading from socket (%d bytes). Last read return=%d\r\n", response_size, r);
	//TODO check response status!

	close(sck);
	return 1;
}

static void thing_speak_task() {
	oap_meas meas;
	while (1) {
		if(xQueuePeek(input_queue, &meas, 1000)) {
			if (post_data(meas)) {
				xQueueReceive(input_queue, &meas, 1000);
				ESP_LOGI(TAG, "data sent successfully");
			} else {
				ESP_LOGW(TAG, "data post failed");
				vTaskDelay(5000 / portTICK_PERIOD_MS);
			}
		}
	}
}

static int thing_speak_configure() {
	cJSON* thingspeak = storage_get_config("thingspeak");
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
		apikey = malloc(strlen(field->valuestring)+1);
		strcpy(apikey,field->valuestring);
		ESP_LOGI(TAG, "apikey: %s", apikey);
		return ESP_OK;
	} else {
		ESP_LOGW(TAG, "apikey not configured");
		return ESP_FAIL;
	}
}

QueueHandle_t thing_speak_init()
{
	if (thing_speak_configure() == ESP_OK) {
		input_queue = xQueueCreate(1, sizeof(oap_meas));
    	xTaskCreate(&thing_speak_task, "thing_speak_task", 1024*10, NULL, 5, NULL);
    	return input_queue;
	} else {
		return NULL;
	}
}
