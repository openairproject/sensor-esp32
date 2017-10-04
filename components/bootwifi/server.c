/*
 * server.c
 *
 *  Created on: Oct 4, 2017
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

#import "server.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include "esp_err.h"
#include "oap_common.h"

#define tag "serv"

typedef enum {
	NOT_RUN = 0,
	IDLE,
	RUNNING,
	RESTARTING
} server_mode_t;

static server_mode_t mode = NOT_RUN;

//static EventGroupHandle_t server_event_group = NULL;
//const int RUNNING_BIT = 0x00000001; //BIT0
//const int POLLING_BIT = 0x00000010; //BIT1


static esp_err_t main_loop(void *mongoose_event_handler) {
	struct mg_mgr mgr;
	struct mg_connection *connection;

	ESP_LOGD(tag, ">> main_loop");
	mg_mgr_init(&mgr, NULL);

	connection = mg_bind(&mgr, ":80", mongoose_event_handler);

	if (connection == NULL) {
		ESP_LOGE(tag, "No connection from the mg_bind().");
		mg_mgr_free(&mgr);
		return ESP_FAIL;
	}

	while (mode == RUNNING) {
		//THIS RETURNS IMMEDIATELY IF THERE'S ISSUE WITH SOCKET?
		//if you see that mongoose task is not responding, this is it
		//TODO we can detect this and restart
		time_t t = mg_mgr_poll(&mgr, 1000);
		//ESP_LOGD(tag,"mongoose listening (%lu)",t);
	}

	mg_mgr_free(&mgr);
	ESP_LOGD(tag, "<< main_loop");
	return ESP_OK;
}

static void server_task(void *mongoose_event_handler) {
	ESP_LOGD(tag, "start");
	while (1) {
		switch (mode) {
			case RUNNING:
				if (main_loop(mongoose_event_handler) != ESP_OK) {
					vTaskDelay(1000 / portTICK_PERIOD_MS);
				}
				break;
			case IDLE:
				vTaskDelay(1000 / portTICK_PERIOD_MS);
				break;
			default:	//{RESTARTING,NOT_RUN} => RUNNING
				mode = RUNNING;
		}
	}
	vTaskDelete(NULL);
}

void server_restart() {
	ESP_LOGD(tag, "restart");
	mode = RESTARTING;
}

void server_stop() {
	ESP_LOGD(tag, "idle");
	mode = IDLE;
}


void server_start(void *event_handler) {
	if (mode == NOT_RUN) {
		mode = RUNNING;
		xTaskCreatePinnedToCore(&server_task, "mongoose_task", 10000, event_handler, DEFAULT_TASK_PRIORITY+1, NULL, 0);
	} else {
		server_restart();
	}
}
