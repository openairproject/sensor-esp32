/**
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_event_loop.h>
#include <esp_wifi.h>
#include <driver/gpio.h>
#include <lwip/sockets.h>
#include "cJSON.h"
#include "bootwifi.h"
#include "sdkconfig.h"
#include <lwip/apps/sntp.h>
#include "oap_common.h"
#include "oap_storage.h"
#include "freertos/event_groups.h"

/**
 * based on https://github.com/nkolban/esp32-snippets/tree/master/networking/bootwifi
 *
 * Check stored wifi settings on startup and turn sensor into Access Point if there's no SSID defined.
 * The same effect can be achieved by pressing down control button during startup.
 *
 * In AP mode, sensor creates OpenAirProject-XXXX network (default password: cleanair),
 * where it listens at http://192.168.1.1:80 and exposes simple html/rest API to modify settings.
 */



#define OAP_ACCESS_POINT_IP "192.168.1.1"
#define OAP_ACCESS_POINT_NETMASK "255.255.255.0"

typedef uint8_t u8_t;
typedef uint16_t u16_t;


oc_wifi_t oap_wifi_config = {
};

wifi_config_t ap_config = {
	.ap = {
		.ssid="",	//constructed later
		.ssid_len=0,
		.password=CONFIG_OAP_AP_PASSWORD, //>= 8 chars
		.channel=0,
		.authmode=WIFI_AUTH_WPA_WPA2_PSK,
		.ssid_hidden=0,
		.max_connection=4,
		.beacon_interval=100
	}
};

// Forward declarations
static int _sntp_initialised = 0;

static char tag[] = "wifi";
static char *hostname = NULL;

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group = NULL;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT 	= 0x00000001; //BIT0
const int DISCONNECTED_BIT 	= 0x00000010; //BIT1
const int STA_MODE_BIT 		= 0x00000100; //BIT2
const int AP_MODE_BIT 		= 0x00001000; //BIT3


static void initialize_sntp(void)
{
	if (_sntp_initialised) return;
	_sntp_initialised = 1;
    ESP_LOGI(tag, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

static esp_err_t set_access_point_ip() {
    tcpip_adapter_ip_info_t info = {};
    inet_pton(AF_INET, OAP_ACCESS_POINT_IP, &info.ip);
    inet_pton(AF_INET, OAP_ACCESS_POINT_NETMASK, &info.netmask);
    inet_pton(AF_INET, OAP_ACCESS_POINT_IP, &info.gw);

    esp_err_t err;

    tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP);
    err = tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &info);
    tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP);
    return err;
}

static void log_wifi_event(void *ctx, system_event_t *event) {
	switch(event->event_id) {
		case  SYSTEM_EVENT_WIFI_READY: 			ESP_LOGI(tag, "SYSTEM_EVENT_WIFI_READY"); break;
		case  SYSTEM_EVENT_SCAN_DONE: 			ESP_LOGI(tag, "SYSTEM_EVENT_SCAN_DONE"); break;
		case  SYSTEM_EVENT_STA_START: 			ESP_LOGI(tag, "SYSTEM_EVENT_STA_START"); break;
		case  SYSTEM_EVENT_STA_STOP: 			ESP_LOGI(tag, "SYSTEM_EVENT_STA_STOP"); break;
		case  SYSTEM_EVENT_STA_CONNECTED: 		ESP_LOGI(tag, "SYSTEM_EVENT_STA_CONNECTED"); break;
		case  SYSTEM_EVENT_STA_DISCONNECTED: 	ESP_LOGI(tag, "SYSTEM_EVENT_STA_DISCONNECTED"); break;
		case  SYSTEM_EVENT_STA_AUTHMODE_CHANGE: ESP_LOGI(tag, "SYSTEM_EVENT_STA_AUTHMODE_CHANGE"); break;
		case  SYSTEM_EVENT_STA_GOT_IP: 			ESP_LOGI(tag, "SYSTEM_EVENT_STA_GOT_IP"); break;
		//esp-idf v2.1+
		//case  SYSTEM_EVENT_STA_LOST_IP: 		ESP_LOGI(tag, "SYSTEM_EVENT_STA_LOST_IP"); break;
		case  SYSTEM_EVENT_AP_START: 			ESP_LOGI(tag, "SYSTEM_EVENT_AP_START"); break;
		case  SYSTEM_EVENT_AP_STOP: 			ESP_LOGI(tag, "SYSTEM_EVENT_AP_STOP"); break;
		case  SYSTEM_EVENT_AP_STACONNECTED: 	ESP_LOGI(tag, "SYSTEM_EVENT_AP_STACONNECTED"); break;
		case  SYSTEM_EVENT_AP_STADISCONNECTED: 	ESP_LOGI(tag, "SYSTEM_EVENT_AP_STADISCONNECTED"); break;
		default : 								ESP_LOGI(tag, "SYSTEM_EVENT_?=%d", event->event_id);
	}
}

static void wifi_state_change(bool connected, bool ap_mode) {
	xEventGroupClearBits(wifi_event_group, (connected ? DISCONNECTED_BIT : CONNECTED_BIT) | (ap_mode ? STA_MODE_BIT : AP_MODE_BIT));
	xEventGroupSetBits(wifi_event_group, (connected ? CONNECTED_BIT : DISCONNECTED_BIT) | (ap_mode ? AP_MODE_BIT : STA_MODE_BIT));
	if (oap_wifi_config.callback) {
		oap_wifi_config.callback(connected, ap_mode);
	}
	ESP_LOGD(tag, "wifi state changed: %s %s", ap_mode ? "AP" : "STA", connected ? "CONNECTED" : "DISCONNECTED");
}

static esp_err_t esp32_wifi_eventHandler(void *ctx, system_event_t *event) {
	log_wifi_event(ctx, event);
	if (is_reboot_in_progress()) {
		ESP_LOGW(tag, "ignore wifi event - reboot in progress");
		return ESP_OK; //ignore - trying to reconnect now will crash
	}

	switch(event->event_id) {
		case SYSTEM_EVENT_AP_START: {
			/*
			 * 192.168.4.1 this is the default IP of all esp devices
			 * http://www.esp8266.com/viewtopic.php?f=29&t=12124
			 * we want to make it 192.168.1.1;
			 * this is probably the best place to put it, however, when we switch from STA->AP
			 * mode, this DHCP manipulation causes restarting AP again (which should be ok)
			 */
			esp_err_t err;
			if ((err=set_access_point_ip()) != ESP_OK) {
				ESP_LOGW(tag, "failed to set ip address [err %x], use default", err);
			}

			tcpip_adapter_ip_info_t ip_info;
			tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info);

			ESP_LOGI(tag, "\n\n*** SENSOR IP (access point '%s') http://"IPSTR"\n", ap_config.ap.ssid, IP2STR(&ip_info.ip));
			wifi_state_change(true, true);
			break;
		}

		case SYSTEM_EVENT_AP_STOP : {
			wifi_state_change(false, true);
			break;
		}

		//WARNING - we cannot rely on this event, sometimes it doesn't got triggered
		case SYSTEM_EVENT_STA_START: {
			esp_wifi_connect();
			break;
		}
		case SYSTEM_EVENT_STA_DISCONNECTED: {
			wifi_state_change(false, false);
			/*
			 * we cannot just switch to APi every time when wifi fails, in most cases we should try to reconnect.
			 * although if the failure happened just after user configured wifi, there's a good chance
			 * that he made a mistake and we should switch to AP.
			 *
			 * TODO remember successful connection to wifi and do not fallback to AP if we ever managed connect to wifi.
			 */
			/*
			 * wifi does not reconnect automatically, we need to do this
			 */
			if (!oap_wifi_config.ap_mode) {
				ESP_LOGI(tag, "reconnect");
				 esp_wifi_connect();
			}
			break;
		}

		case SYSTEM_EVENT_STA_GOT_IP: {
			wifi_mode_t mode;
			esp_err_t ret;
			if ((ret = esp_wifi_get_mode(&mode)) != ESP_OK) {
				ESP_LOGE(tag, "esp_wifi_get_mode failed  0x%x", ret);
				return ret;
			}

			switch (mode) {
				case WIFI_MODE_STA:
					ESP_LOGI(tag, "\n\n*** SENSOR IP (network '%s') http://"IPSTR"\n", oap_wifi_config.ssid, IP2STR(&event->event_info.got_ip.ip_info.ip));

					initialize_sntp();
					/*
					 * TODO if we attempt to make an SSL request (by OTA, didn't check others) when wifi is in AP mode,
					 * mongoose goes into infinite loop;
					 * socket.accept fails immediately. this may be network stack bug.
					 *
					 * TODO this should RESTART mongoose!
					 */
					//start_mongoose();
					wifi_state_change(true, false);

					break;
				case WIFI_MODE_AP:
					//after v2.1, in AP mode, SYSTEM_EVENT_STA_GOT_IP is fired! it does not happen <= 2.1
					//ESP_LOGI(tag, "****** SENSOR IP (AP %s) http://"IPSTR, ap_config.ap.ssid, IP2STR(&event->event_info.got_ip.ip_info.ip));
					//wifi_state_change(true, true);
					break;
				default:
					ESP_LOGW(tag, "unsupported wifi mode: %d", mode);
			}

			break;
		}

		default:
			break;
	}

	return ESP_OK;
}


esp_err_t wifi_configure(cJSON* wifi, wifi_state_callback_f wifi_state_callback) {
	memset(&oap_wifi_config, 0, sizeof(oc_wifi_t));
	oap_wifi_config.callback = wifi_state_callback;

	if (wifi) {
		cJSON* field;
		if ((field = cJSON_GetObjectItem(wifi, "ssid"))) strcpy(oap_wifi_config.ssid, field->valuestring);

		if (strlen(oap_wifi_config.ssid) == 0) {
			oap_wifi_config.ap_mode = 1;
			return ESP_FAIL;
		}

		if ((field = cJSON_GetObjectItem(wifi, "password"))) strcpy(oap_wifi_config.password, field->valuestring);

		if ((field = cJSON_GetObjectItem(wifi, "ip"))) {
			inet_pton(AF_INET, field->valuestring, &oap_wifi_config.ipInfo.ip);
		}
		if ((field = cJSON_GetObjectItem(wifi, "gw"))) {
			inet_pton(AF_INET, field->valuestring, &oap_wifi_config.ipInfo.gw);
		}
		if ((field = cJSON_GetObjectItem(wifi, "netmask"))) {
			inet_pton(AF_INET, field->valuestring, &oap_wifi_config.ipInfo.netmask);
		}
		if ((field = cJSON_GetObjectItem(wifi, "sensorId")) && field->valuestring && strlen(field->valuestring)) {
			set_config_str_field(&hostname, field->valuestring);
		}
		ESP_LOGD(tag, "wifi.ssid: %s", oap_wifi_config.ssid);
		ESP_LOGD(tag, "wifi.pass.lenght: [%d]", strlen(oap_wifi_config.password));

		ESP_LOGD(tag, "wifi.ip:" IPSTR, IP2STR(&oap_wifi_config.ipInfo.ip));
		ESP_LOGD(tag, "wifi.gateway:" IPSTR, IP2STR(&oap_wifi_config.ipInfo.gw));
		ESP_LOGD(tag, "wifi.netmask:" IPSTR, IP2STR(&oap_wifi_config.ipInfo.netmask));
	} else {
		oap_wifi_config.ap_mode = 1;
	}
	return ESP_OK;
}

static void station_watcher() {
	int retry_counter=0;	
	ESP_LOGI(tag, "station_watcher started"); 
	while(1) {
		if(retry_counter < 6) {
			if(wifi_disconnected_wait_for(10) == ESP_OK) {
				retry_counter++;
				ESP_LOGW(tag, "WIFI disconnected since %ds, retry #%d", retry_counter * 10, retry_counter);
			} else {
				retry_counter=0;
			}
		} else {
			oap_reboot("WIFI disconnected, retries exceeded, restarting");
		}
		delay(10000);
	}
}

static void become_station() {
	ESP_LOGD(tag, "- Connecting to access point \"%s\" ...", oap_wifi_config.ssid);
	assert(strlen(oap_wifi_config.ssid) > 0);

	// If we have a static IP address information, use that.
	if (oap_wifi_config.ipInfo.ip.addr != 0) {
		ESP_LOGD(tag, " - using a static IP address of " IPSTR, IP2STR(&oap_wifi_config.ipInfo.ip));
		tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA);
		tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &oap_wifi_config.ipInfo);
	} else {
		tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_STA);
	}

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	wifi_config_t sta_config = {0};
	sta_config.sta.bssid_set = 0;
	memcpy(sta_config.sta.ssid, oap_wifi_config.ssid, SSID_SIZE);
	memcpy(sta_config.sta.password, oap_wifi_config.password, PASSWORD_SIZE);
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
	ESP_ERROR_CHECK(esp_wifi_start());
	esp_err_t err = tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, hostname);
	if (err){
		ESP_LOGI(tag, "tcpip_adapter_set_hostname failed, rc=%d", err);
	}
	xTaskCreate(station_watcher, "station_watcher", 2*1024, NULL, DEFAULT_TASK_PRIORITY, NULL);
}

static void become_access_point() {
	ESP_LOGD(tag, "- Starting being an access point ...");
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

	get_generic_name((char *)ap_config.ap.ssid);

	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
	ESP_ERROR_CHECK(esp_wifi_start());
}

static void init_wifi() {
	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_init(esp32_wifi_eventHandler, NULL));
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
}

void wifi_boot() {
	ESP_LOGD(tag, "wifi_boot start");

	if (!wifi_event_group) {
		wifi_event_group = xEventGroupCreate();
		init_wifi();
	}

	if (oap_wifi_config.ap_mode) {
		become_access_point();
	} else {
		become_station();
	}
}

esp_err_t wifi_disconnected_wait_for(uint32_t ms) {
	return
			xEventGroupWaitBits(wifi_event_group, DISCONNECTED_BIT,
			false, true, ms ? ms / portTICK_PERIOD_MS : portMAX_DELAY) & DISCONNECTED_BIT ? ESP_OK : ESP_FAIL;
}

esp_err_t wifi_connected_wait_for(uint32_t ms) {
	return
			xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT | STA_MODE_BIT,
			false, true, ms ? ms / portTICK_PERIOD_MS : portMAX_DELAY) == (CONNECTED_BIT | STA_MODE_BIT) ? ESP_OK : ESP_FAIL;
}

esp_err_t wifi_ap_started_wait_for(uint32_t ms) {
	return
			xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT | AP_MODE_BIT,
			false, true, ms ? ms / portTICK_PERIOD_MS : portMAX_DELAY) == (CONNECTED_BIT | AP_MODE_BIT) ? ESP_OK : ESP_FAIL;
}
