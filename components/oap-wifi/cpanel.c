/*
 * cpanel.c
 *
 *  Created on: Oct 5, 2017
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

#include "oap_common.h"
#include "mongoose.h"
#include "cJSON.h"
#include "oap_storage.h"
#include "pm_meter.h"

#define tag "cpanel"

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

extern pm_data_pair_t pm_data_array;
extern env_data_record_t last_env_data[3];

static char *mgStrToStr(struct mg_str mgStr) {
	char *retStr = (char *) malloc(mgStr.len + 1);
	memcpy(retStr, mgStr.p, mgStr.len);
	retStr[mgStr.len] = 0;
	return retStr;
} // mgStrToStr

static void handler_index(struct mg_connection *nc) {
	size_t resp_size = index_html_end-index_html_start;
	mg_send_head(nc, 200, resp_size, "Content-Type: text/html");
	mg_send(nc, index_html_start, resp_size);
	ESP_LOGD(tag, "served %d bytes", resp_size);
}

static void handler_get_config(struct mg_connection *nc, struct http_message *message) {
	ESP_LOGD(tag, "handler_get_config");
	cJSON* config = storage_get_config_to_update();
	char* json = cJSON_Print(config);
	char* headers = malloc(200);
	sprintf(headers, "Content-Type: application/json\r\nX-Version: %s", oap_version_str());
	mg_send_head(nc, 200, strlen(json), headers);
	mg_send(nc, json, strlen(json));
	free(headers);
	free(json);
}

static void handler_get_status(struct mg_connection *nc, struct http_message *message) {
	ESP_LOGD(tag, "handler_get_status");
	cJSON *root, *status, *data;

	root = cJSON_CreateObject();
	status = cJSON_CreateObject();
	data = cJSON_CreateObject();

	cJSON_AddItemToObject(root, "status", status);
	cJSON_AddItemToObject(root, "data", data);

	cJSON_AddItemToObject(status, "version", cJSON_CreateString(oap_version_str()));

	time_t now = time(NULL);
	struct tm timeinfo = {0};

	localtime_r(&now, &timeinfo);
        if(timeinfo.tm_year > (2016 - 1900)) {
		time_t boot_time = now-(xTaskGetTickCount()/configTICK_RATE_HZ);
#if 0
                setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
                tzset();
#endif                
                char strftime_buf[64];
                strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
                cJSON_AddItemToObject(status, "utctime", cJSON_CreateString(strftime_buf));
                cJSON_AddItemToObject(status, "uptime", cJSON_CreateNumber(now - boot_time));
                cJSON_AddItemToObject(status, "heap", cJSON_CreateNumber(esp_get_free_heap_size()));
	}
	if(CONFIG_OAP_BMX280_ENABLED) {
		cJSON *envobj0 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env0", envobj0);
		cJSON_AddItemToObject(envobj0, "temp", cJSON_CreateNumber(last_env_data[0].env_data.temp));
		cJSON_AddItemToObject(envobj0, "pressure", cJSON_CreateNumber(last_env_data[0].env_data.pressure));	
		cJSON_AddItemToObject(envobj0, "humidity", cJSON_CreateNumber(last_env_data[0].env_data.humidity));
	}
	if(CONFIG_OAP_BMX280_ENABLED_AUX) {
		cJSON *envobj1 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env1", envobj1);
		cJSON_AddItemToObject(envobj1, "temp", cJSON_CreateNumber(last_env_data[1].env_data.temp));
		cJSON_AddItemToObject(envobj1, "pressure", cJSON_CreateNumber(last_env_data[1].env_data.pressure));	
		cJSON_AddItemToObject(envobj1, "humidity", cJSON_CreateNumber(last_env_data[1].env_data.humidity));
	}
	if(CONFIG_OAP_MH_ENABLED) {
		cJSON *envobj2 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env2", envobj2);
		cJSON_AddItemToObject(envobj2, "co2", cJSON_CreateNumber(last_env_data[2].env_data.co2));
	}
	cJSON *pmobj0 = cJSON_CreateObject();
	cJSON_AddItemToObject(data, "pm0", pmobj0);
	cJSON_AddItemToObject(pmobj0, "pm1_0", cJSON_CreateNumber(pm_data_array.pm_data[0].pm1_0));
	cJSON_AddItemToObject(pmobj0, "pm2_5", cJSON_CreateNumber(pm_data_array.pm_data[0].pm2_5));
	cJSON_AddItemToObject(pmobj0, "pm10", cJSON_CreateNumber(pm_data_array.pm_data[0].pm10));
	if(CONFIG_OAP_PM_ENABLED_AUX) {
		cJSON *pmobj1 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "pm1", pmobj1);
		cJSON_AddItemToObject(pmobj1, "pm1_0", cJSON_CreateNumber(pm_data_array.pm_data[1].pm1_0));
		cJSON_AddItemToObject(pmobj1, "pm2_5", cJSON_CreateNumber(pm_data_array.pm_data[1].pm2_5));
		cJSON_AddItemToObject(pmobj1, "pm10", cJSON_CreateNumber(pm_data_array.pm_data[1].pm10));
	}
	char* json = cJSON_Print(root);
	mg_send(nc, json, strlen(json));
	free(json);
	cJSON_Delete(root);
}

static void handler_reboot(struct mg_connection *nc) {
	mg_send_head(nc, 200, 0, "Content-Type: text/plain");
	oap_reboot("requested by user");
}

static void handler_set_config(struct mg_connection *nc, struct http_message *message) {
	ESP_LOGD(tag, "handler_set_config");
	char *body = mgStrToStr(message->body);
	cJSON* config = cJSON_Parse(body);
	free(body);
	if (config) {
		storage_update_config(config);
		handler_get_config(nc, message);
	} else {
		mg_http_send_error(nc, 500, "invalid config");
	}
	cJSON_Delete(config);
}

/**
 * Handle mongoose events.  These are mostly requests to process incoming
 * browser requests.  The ones we handle are:
 * GET / - Send the enter details page.
 * GET /set - Set the connection info (REST request).
 * POST /ssidSelected - Set the connection info (HTML FORM).
 */
void cpanel_event_handler(struct mg_connection *nc, int ev, void *evData) {
	ESP_LOGV(tag, "- Event: %d", ev);
	uint8_t handled = 0;
	switch (ev) {
		case MG_EV_HTTP_REQUEST: {
			struct http_message *message = (struct http_message *) evData;

			//mg_str is not terminated with '\0'
			char *uri = mgStrToStr(message->uri);
			char *method = mgStrToStr(message->method);

			ESP_LOGD(tag, "%s %s", method, uri);

			if (strcmp(uri, "/") == 0) {
				handler_index(nc);
				handled = 1;
			}
			if (strcmp(uri, "/reboot") == 0) {
				handler_reboot(nc);
				handled = 1;
			}
			if(strcmp(uri, "/config") == 0) {
				if (strcmp(method, "GET") == 0) {
					handler_get_config(nc, message);
					handled = 1;
				} else if (strcmp(method, "POST") == 0) {
					handler_set_config(nc, message);
					handled = 1;
				}
			}
			if(strcmp(uri, "/status") == 0) {
				if (strcmp(method, "GET") == 0) {
					handler_get_status(nc, message);
					handled = 1;
				} else if (strcmp(method, "POST") == 0) {
					handler_get_status(nc, message);
					handled = 1;
				}
			}

			if (!handled) {
				mg_send_head(nc, 404, 0, "Content-Type: text/plain");
			}
			nc->flags |= MG_F_SEND_AND_CLOSE;
			free(uri);
			free(method);
			break;
		}
	}
}
