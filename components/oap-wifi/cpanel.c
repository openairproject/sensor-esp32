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
#include "mhz19.h"
#include "hw_gpio.h"

#define tag "cpanel"

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

inline int ishex(int x) {
	return	(x >= '0' && x <= '9')	||
		(x >= 'a' && x <= 'f')	||
		(x >= 'A' && x <= 'F');
}
 
static int decode(const char *s, char *dec) {
	char *o;
	const char *end = s + strlen(s);
	int c;
 
	for (o = dec; s <= end; o++) {
		c = *s++;
		if (c == '+') c = ' ';
		else if (c == '%' && (	!ishex(*s++)	||
					!ishex(*s++)	||
					!sscanf(s - 2, "%2x", &c)))
			return -1;
 
		if (dec) *o = c;
	}
 
	return o - dec;
}

static int parse_query(char *str, char *key, char *val, size_t szval) {
	char *delimiter1 = "&\r\n ";
	char *delimiter2 = "=";
	
	char *ptr1;
	char *ptr2;
	char *saveptr1;
	char *saveptr2;
	ptr1 = strtok_r(str, delimiter1, &saveptr1);
//	ESP_LOGD(tag, "-->parse_query: str:%s key:%s ptr1:%s", str, key, ptr1);
	while(ptr1 != NULL) {
		ptr2 = strtok_r(ptr1, delimiter2, &saveptr2);
		if(ptr1 && ptr2) {
//			ESP_LOGD(tag, "ptr1: %s ptr2: %s", ptr1, ptr2);
		}
		while(ptr2 != NULL) {
			char *tmp=ptr2;
			ptr2=strtok_r(NULL, delimiter2, &saveptr2);
			if(ptr2) {
//				ESP_LOGD(tag, "ptr2: %s", ptr2);
			}
			if(!strcmp(tmp, key)) {
//				ESP_LOGD(tag, "parse_query: %s == %s", tmp, key);
				memset(val, 0, szval);
				decode(ptr2, val);
//				ESP_LOGD(tag, "parse_query: val:%s", val);				
				return 1;
			} 
		}
		ptr1 = strtok_r(NULL, delimiter1, &saveptr1);		
	}
	return 0;
}

static int parse_query_int(char *query, char *key, int *val) {
	char valstr[32];
	char *str = strdup(query);
	int ret=parse_query(str, key, valstr, sizeof(valstr));
	ESP_LOGI(tag, "parse_query_int: %s %s -> %s", str, key,  valstr);
	if(ret) {
		*val = atoi(valstr); 
	} else {
		*val=0;
	}
	free(str);
	return ret;
}

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
	time_t boot_time = now-(xTaskGetTickCount()/configTICK_RATE_HZ);
	long sysTime = oap_epoch_sec();
        if(timeinfo.tm_year > (2016 - 1900)) {
#if 0
                setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
                tzset();
#endif                
                char strftime_buf[64];
                strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
                cJSON_AddItemToObject(status, "utctime", cJSON_CreateString(strftime_buf));
	}
	cJSON_AddItemToObject(status, "time", cJSON_CreateNumber(now));
	cJSON_AddItemToObject(status, "uptime", cJSON_CreateNumber(now - boot_time));
	cJSON_AddItemToObject(status, "heap", cJSON_CreateNumber(esp_get_free_heap_size()));
	#ifdef CONFIG_OAP_BMX280_ENABLED
	if(last_env_data[0].timestamp) {
		cJSON *envobj0 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env0", envobj0);
		cJSON_AddItemToObject(envobj0, "temp", cJSON_CreateNumber(last_env_data[0].env_data.bmx280.temp));
		cJSON_AddItemToObject(envobj0, "pressure", cJSON_CreateNumber(last_env_data[0].env_data.bmx280.sealevel));	
		cJSON_AddItemToObject(envobj0, "humidity", cJSON_CreateNumber(last_env_data[0].env_data.bmx280.humidity));
		cJSON_AddItemToObject(envobj0, "timestamp", cJSON_CreateNumber(sysTime - last_env_data[0].timestamp));
	}
	#endif
	#ifdef CONFIG_OAP_BMX280_ENABLED_AUX
	if(last_env_data[1].timestamp) {
		cJSON *envobj1 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env1", envobj1);
		cJSON_AddItemToObject(envobj1, "temp", cJSON_CreateNumber(last_env_data[1].env_data.bmx280.temp));
		cJSON_AddItemToObject(envobj1, "pressure", cJSON_CreateNumber(last_env_data[1].env_data.bmx280.sealevel));	
		cJSON_AddItemToObject(envobj1, "humidity", cJSON_CreateNumber(last_env_data[1].env_data.bmx280.humidity));
		cJSON_AddItemToObject(envobj1, "timestamp", cJSON_CreateNumber(sysTime - last_env_data[1].timestamp));	
	}
	#endif
	#ifdef CONFIG_OAP_MH_ENABLED
	if(last_env_data[2].timestamp) {
		cJSON *envobj2 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env2", envobj2);
		cJSON_AddItemToObject(envobj2, "co2", cJSON_CreateNumber(last_env_data[2].env_data.mhz19.co2));
		cJSON_AddItemToObject(envobj2, "timestamp", cJSON_CreateNumber(sysTime - last_env_data[2].timestamp));	
	}
	#endif
	#ifdef CONFIG_OAP_HCSR04_0_ENABLED 
	if(last_env_data[3].timestamp) {
		cJSON *envobj3 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env3", envobj3);
		cJSON_AddItemToObject(envobj3, "distance", cJSON_CreateNumber(last_env_data[3].env_data.hcsr04.distance));
		cJSON_AddItemToObject(envobj3, "timestamp", cJSON_CreateNumber(sysTime - last_env_data[3].timestamp));	
	}
	#endif
	#ifdef CONFIG_OAP_HCSR04_1_ENABLED
	if(last_env_data[4].timestamp) {
		cJSON *envobj4 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env4", envobj4);
		cJSON_AddItemToObject(envobj4, "distance", cJSON_CreateNumber(last_env_data[4].env_data.hcsr04.distance));
		cJSON_AddItemToObject(envobj4, "timestamp", cJSON_CreateNumber(sysTime - last_env_data[4].timestamp));	
	}
	#endif
	#ifdef CONFIG_OAP_GPIO_0_ENABLED
	if(last_env_data[5].timestamp) {
		cJSON *envobj5 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env5", envobj5);
		cJSON_AddItemToObject(envobj5, "GPIlastLow", cJSON_CreateNumber(last_env_data[5].env_data.gpio.GPIlastLow));
		cJSON_AddItemToObject(envobj5, "GPIlastHigh", cJSON_CreateNumber(last_env_data[5].env_data.gpio.GPIlastHigh));
		cJSON_AddItemToObject(envobj5, "GPICounter", cJSON_CreateNumber(last_env_data[5].env_data.gpio.GPICounter));
		cJSON_AddItemToObject(envobj5, "GPOlastOut", cJSON_CreateNumber(last_env_data[5].env_data.gpio.GPOlastOut));
		cJSON_AddItemToObject(envobj5, "GPOlastVal", cJSON_CreateNumber(last_env_data[5].env_data.gpio.GPOlastVal));
		cJSON_AddItemToObject(envobj5, "timestamp", cJSON_CreateNumber(sysTime - last_env_data[5].timestamp));	
	}
	#endif
	#ifdef CONFIG_OAP_GPIO_1_ENABLED
	if(last_env_data[6].timestamp) {
		cJSON *envobj6 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env6", envobj6);
		cJSON_AddItemToObject(envobj6, "GPIlastLow", cJSON_CreateNumber(last_env_data[6].env_data.gpio.GPIlastLow));
		cJSON_AddItemToObject(envobj6, "GPICounter", cJSON_CreateNumber(last_env_data[6].env_data.gpio.GPICounter));
		cJSON_AddItemToObject(envobj6, "GPIlastHigh", cJSON_CreateNumber(last_env_data[6].env_data.gpio.GPIlastHigh));
		cJSON_AddItemToObject(envobj6, "GPOlastOut", cJSON_CreateNumber(last_env_data[6].env_data.gpio.GPOlastOut));
		cJSON_AddItemToObject(envobj6, "GPOlastVal", cJSON_CreateNumber(last_env_data[6].env_data.gpio.GPOlastVal));
		cJSON_AddItemToObject(envobj6, "timestamp", cJSON_CreateNumber(sysTime - last_env_data[6].timestamp));			
	}
	#endif
	#ifdef CONFIG_OAP_GPIO_2_ENABLED
	if(last_env_data[7].timestamp) {
		cJSON *envobj7 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env7", envobj7);
		cJSON_AddItemToObject(envobj7, "GPIlastLow", cJSON_CreateNumber(last_env_data[7].env_data.gpio.GPIlastLow));
		cJSON_AddItemToObject(envobj7, "GPIlastHigh", cJSON_CreateNumber(last_env_data[7].env_data.gpio.GPIlastHigh));
		cJSON_AddItemToObject(envobj7, "GPICounter", cJSON_CreateNumber(last_env_data[7].env_data.gpio.GPICounter));
		cJSON_AddItemToObject(envobj7, "GPOlastOut", cJSON_CreateNumber(last_env_data[7].env_data.gpio.GPOlastOut));
		cJSON_AddItemToObject(envobj7, "GPOlastVal", cJSON_CreateNumber(last_env_data[7].env_data.gpio.GPOlastVal));
		cJSON_AddItemToObject(envobj7, "timestamp", cJSON_CreateNumber(sysTime - last_env_data[7].timestamp));	
	}
	#endif
	#ifdef CONFIG_OAP_GPIO_3_ENABLED
	if(last_env_data[8].timestamp) {
		cJSON *envobj8 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env8", envobj8);
		cJSON_AddItemToObject(envobj8, "GPIlastLow", cJSON_CreateNumber(last_env_data[8].env_data.gpio.GPIlastLow));
		cJSON_AddItemToObject(envobj8, "GPIlastHigh", cJSON_CreateNumber(last_env_data[8].env_data.gpio.GPIlastHigh));
		cJSON_AddItemToObject(envobj8, "GPICounter", cJSON_CreateNumber(last_env_data[8].env_data.gpio.GPICounter));
		cJSON_AddItemToObject(envobj8, "GPOlastOut", cJSON_CreateNumber(last_env_data[8].env_data.gpio.GPOlastOut));
		cJSON_AddItemToObject(envobj8, "GPOlastVal", cJSON_CreateNumber(last_env_data[8].env_data.gpio.GPOlastVal));
		cJSON_AddItemToObject(envobj8, "timestamp", cJSON_CreateNumber(sysTime - last_env_data[8].timestamp));			
	}
	#endif
	#ifdef CONFIG_OAP_PM_UART_ENABLE
	if(pm_data_array.timestamp) {
		cJSON *pmobj0 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "pm0", pmobj0);
		cJSON_AddItemToObject(pmobj0, "pm1_0", cJSON_CreateNumber(pm_data_array.pm_data[0].pm1_0));
		cJSON_AddItemToObject(pmobj0, "pm2_5", cJSON_CreateNumber(pm_data_array.pm_data[0].pm2_5));
		cJSON_AddItemToObject(pmobj0, "pm10", cJSON_CreateNumber(pm_data_array.pm_data[0].pm10));
		cJSON_AddItemToObject(pmobj0, "timestamp", cJSON_CreateNumber(sysTime - pm_data_array.timestamp));
	#ifdef CONFIG_OAP_PM_UART_AUX_ENABLE
		cJSON *pmobj1 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "pm1", pmobj1);
		cJSON_AddItemToObject(pmobj1, "pm1_0", cJSON_CreateNumber(pm_data_array.pm_data[1].pm1_0));
		cJSON_AddItemToObject(pmobj1, "pm2_5", cJSON_CreateNumber(pm_data_array.pm_data[1].pm2_5));
		cJSON_AddItemToObject(pmobj1, "pm10", cJSON_CreateNumber(pm_data_array.pm_data[1].pm10));
	#endif
	}
	#endif
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
			char *query_string = mgStrToStr(message->query_string);
			
			ESP_LOGD(tag, "%s %s %s", method, uri, query_string);

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
			if(strcmp(uri, "/calibrate") == 0) {
				char *str="ok\n";
				int len=strlen(str);
				mg_send_head(nc, 200, len, "Content-Type: text/plain");
				mhz19_calibrate(&mhz19_cfg[0]);
				mg_send(nc, str, len);
				handled = 1;
			}
			if(strcmp(uri, "/trigger") == 0) {
				int delay;
				int value;
				int gpio; 
				char *str="ok\n";
				int len=strlen(str);
				mg_send_head(nc, 200, len, "Content-Type: text/plain");
				parse_query_int(query_string, "delay", &delay);
				parse_query_int(query_string, "value", &value);
				parse_query_int(query_string, "gpio", &gpio);
				if(gpio >= 0 && gpio < HW_GPIO_DEVICES_MAX) {
					hw_gpio_send_trigger(&hw_gpio_cfg[gpio], value, delay);
				}
				mg_send(nc,str, len);
				handled = 1;
			}
			if (!handled) {
			}
			nc->flags |= MG_F_SEND_AND_CLOSE;
			free(uri);
			free(method);
			free(query_string);
			break;
		}
	}
}
