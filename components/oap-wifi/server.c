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

#include "server.h"
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

extern int mg_invalid_socket;

static server_mode_t mode = NOT_RUN;

/*
char *mongoose_eventToString(int ev) {
	static char temp[100];
	switch (ev) {
	case MG_EV_CONNECT:
		return "MG_EV_CONNECT";
	case MG_EV_ACCEPT:
		return "MG_EV_ACCEPT";
	case MG_EV_CLOSE:
		return "MG_EV_CLOSE";
	case MG_EV_SEND:
		return "MG_EV_SEND";
	case MG_EV_RECV:
		return "MG_EV_RECV";
	case MG_EV_HTTP_REQUEST:
		return "MG_EV_HTTP_REQUEST";
	case MG_EV_MQTT_CONNACK:
		return "MG_EV_MQTT_CONNACK";
	case MG_EV_MQTT_CONNACK_ACCEPTED:
		return "MG_EV_MQTT_CONNACK";
	case MG_EV_MQTT_CONNECT:
		return "MG_EV_MQTT_CONNECT";
	case MG_EV_MQTT_DISCONNECT:
		return "MG_EV_MQTT_DISCONNECT";
	case MG_EV_MQTT_PINGREQ:
		return "MG_EV_MQTT_PINGREQ";
	case MG_EV_MQTT_PINGRESP:
		return "MG_EV_MQTT_PINGRESP";
	case MG_EV_MQTT_PUBACK:
		return "MG_EV_MQTT_PUBACK";
	case MG_EV_MQTT_PUBCOMP:
		return "MG_EV_MQTT_PUBCOMP";
	case MG_EV_MQTT_PUBLISH:
		return "MG_EV_MQTT_PUBLISH";
	case MG_EV_MQTT_PUBREC:
		return "MG_EV_MQTT_PUBREC";
	case MG_EV_MQTT_PUBREL:
		return "MG_EV_MQTT_PUBREL";
	case MG_EV_MQTT_SUBACK:
		return "MG_EV_MQTT_SUBACK";
	case MG_EV_MQTT_SUBSCRIBE:
		return "MG_EV_MQTT_SUBSCRIBE";
	case MG_EV_MQTT_UNSUBACK:
		return "MG_EV_MQTT_UNSUBACK";
	case MG_EV_MQTT_UNSUBSCRIBE:
		return "MG_EV_MQTT_UNSUBSCRIBE";
	case MG_EV_WEBSOCKET_HANDSHAKE_REQUEST:
		return "MG_EV_WEBSOCKET_HANDSHAKE_REQUEST";
	case MG_EV_WEBSOCKET_HANDSHAKE_DONE:
		return "MG_EV_WEBSOCKET_HANDSHAKE_DONE";
	case MG_EV_WEBSOCKET_FRAME:
		return "MG_EV_WEBSOCKET_FRAME";
	}
	sprintf(temp, "Unknown event: %d", ev);
	return temp;
}*/


static esp_err_t main_loop(void *mongoose_event_handler) {
	struct mg_mgr mgr;
	struct mg_connection *connection;

	ESP_LOGD(tag, ">> main_loop");
	mg_mgr_init(&mgr, NULL);

	connection = mg_bind(&mgr, ":80", mongoose_event_handler);

	if (connection == NULL) {
		//when this happens usually it won't recover until it gets a new IP
		//maybe we should reboot?
		ESP_LOGW(tag, "No connection from the mg_bind().");
		mg_mgr_free(&mgr);
		oap_reboot("No connection from the mg_bind().");
		return ESP_FAIL;
	}
	//use http
	mg_set_protocol_http_websocket(connection);

	mg_invalid_socket=0; //hack for corrupted mongoose sockets (AP mode + http request triggers it)
	while (mode == RUNNING && !mg_invalid_socket) {
		mg_mgr_poll(&mgr, 1000);
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
