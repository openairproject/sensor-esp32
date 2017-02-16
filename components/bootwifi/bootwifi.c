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
#include <tcpip_adapter.h>
#include <lwip/sockets.h>
#include "mongoose.h"
#include "bootwifi.h"
#include "sdkconfig.h"
#include "apps/sntp/sntp.h"
#include "http_utils.h"
#include "oap_storage.h"

/**
 * based on https://github.com/nkolban/esp32-snippets/tree/master/networking/bootwifi
 *
 * Check stored wifi settings on startup and turn sensor into Access Point if there's no SSID defined.
 * The same effect can be achieved by pressing down control button during startup.
 *
 * In AP mode, sensor creates OpenAirProject-XXXX network (default password: cleanair),
 * where it listens at http://192.168.1.1:80 and exposes simple html/rest API to modify settings.
 */

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

#define SSID_SIZE (32) // Maximum SSID size
#define PASSWORD_SIZE (64) // Maximum password size

#define OAP_ACCESS_POINT_IP "192.168.1.1"
#define OAP_ACCESS_POINT_NETMASK "255.255.255.0"


typedef struct {
	char ssid[SSID_SIZE];
	char password[PASSWORD_SIZE];
	tcpip_adapter_ip_info_t ipInfo; // Optional static IP information
} oc_wifi_t;

typedef uint8_t u8_t;
typedef uint16_t u16_t;

static int g_mongooseStarted = 0; // Has the mongoose server started?
static int g_mongooseStopRequest = 0; // Request to stop the mongoose server.

// Forward declarations
static int is_station = 0;
static void become_access_point();
static void restore_wifi_setup();

static char tag[] = "wifi";

static void initialize_sntp(void)
{
    ESP_LOGI(tag, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

static void handler_index(struct mg_connection *nc) {
	size_t resp_size = index_html_end-index_html_start;
	mg_send_head(nc, 200, resp_size, "Content-Type: text/html");
	mg_send(nc, index_html_start, resp_size);
}

static void handler_get_config(struct mg_connection *nc, struct http_message *message) {
	ESP_LOGD(tag, "handler_get_config");
	char* json = storage_get_config_str();
	if (json) {
		mg_send_head(nc, 200, strlen(json), "Content-Type: application/json");
		mg_send(nc, json, strlen(json));
		free(json);
	} else {
		mg_http_send_error(nc, 500, "failed to load config");
	}
}

static void handler_reboot(struct mg_connection *nc) {
	mg_send_head(nc, 200, 0, "Content-Type: text/plain");
	ESP_LOGW(tag, "received reboot request!");
	esp_restart();
}

static void handler_set_config(struct mg_connection *nc, struct http_message *message) {
	ESP_LOGD(tag, "handler_set_config");
	char *body = mgStrToStr(message->body);
	if (storage_set_config_str(body) == ESP_OK) {
		handler_get_config(nc, message);
		//reboot_delayed();
	} else {
		mg_http_send_error(nc, 500, "failed to store config");
	}
	free(body);
}

/**
 * Handle mongoose events.  These are mostly requests to process incoming
 * browser requests.  The ones we handle are:
 * GET / - Send the enter details page.
 * GET /set - Set the connection info (REST request).
 * POST /ssidSelected - Set the connection info (HTML FORM).
 */
static void mongoose_event_handler(struct mg_connection *nc, int ev, void *evData) {
	ESP_LOGV(tag, "- Event: %s", mongoose_eventToString(ev));
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


// FreeRTOS task to start Mongoose.
static void mongooseTask(void *data) {
	struct mg_mgr mgr;
	struct mg_connection *connection;

	ESP_LOGD(tag, ">> mongooseTask");
	g_mongooseStopRequest = 0; // Unset the stop request since we are being asked to start.

	mg_mgr_init(&mgr, NULL);

	connection = mg_bind(&mgr, ":80", mongoose_event_handler);

	if (connection == NULL) {
		ESP_LOGE(tag, "No connection from the mg_bind().");
		mg_mgr_free(&mgr);
		ESP_LOGD(tag, "<< mongooseTask");
		vTaskDelete(NULL);
		return;
	}
	mg_set_protocol_http_websocket(connection);

	// Keep processing until we are flagged that there is a stop request.
	while (!g_mongooseStopRequest) {
		mg_mgr_poll(&mgr, 1000);
	}

	// We have received a stop request, so stop being a web server.
	mg_mgr_free(&mgr);
	g_mongooseStarted = 0;

	ESP_LOGD(tag, "<< mongooseTask");
	vTaskDelete(NULL);
	return;
} // mongooseTask

int set_access_point_ip()
{
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


/**
 * An ESP32 WiFi event handler.
 * The types of events that can be received here are:
 *
 * SYSTEM_EVENT_AP_PROBEREQRECVED
 * SYSTEM_EVENT_AP_STACONNECTED
 * SYSTEM_EVENT_AP_STADISCONNECTED
 * SYSTEM_EVENT_AP_START
 * SYSTEM_EVENT_AP_STOP
 * SYSTEM_EVENT_SCAN_DONE
 * SYSTEM_EVENT_STA_AUTHMODE_CHANGE
 * SYSTEM_EVENT_STA_CONNECTED
 * SYSTEM_EVENT_STA_DISCONNECTED
 * SYSTEM_EVENT_STA_GOT_IP
 * SYSTEM_EVENT_STA_START
 * SYSTEM_EVENT_STA_STOP
 * SYSTEM_EVENT_WIFI_READY
 */
static esp_err_t esp32_wifi_eventHandler(void *ctx, system_event_t *event) {
	// Your event handling code here...
	switch(event->event_id) {
		// When we have started being an access point, then start being a web server.
		case SYSTEM_EVENT_AP_START: { // Handle the AP start event
			esp_err_t err;
			if ((err=set_access_point_ip()) != ESP_OK) {
				ESP_LOGW(tag, "failed to set ip address [err %x], use default", err);
			}
			tcpip_adapter_ip_info_t ip_info;
			tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info);

			//192.168.4.1 this is the default IP of all esp devices
			//http://www.esp8266.com/viewtopic.php?f=29&t=12124

			ESP_LOGI(tag, "**********************************************");
			ESP_LOGI(tag, "* ACCESS POINT MODE")
			ESP_LOGI(tag, "* point your browser to http://"IPSTR, IP2STR(&ip_info.ip));
			ESP_LOGI(tag, "**********************************************");

			// Start Mongoose ...
			if (!g_mongooseStarted)
			{
				g_mongooseStarted = 1;
				xTaskCreatePinnedToCore(&mongooseTask, "bootwifi_mongoose_task", 10000, NULL, 5, NULL, 0);
			}
			break;
		} // SYSTEM_EVENT_AP_START

		// If we fail to connect to an access point as a station, become an access point.
		case SYSTEM_EVENT_STA_DISCONNECTED: {
			ESP_LOGD(tag, "Station disconnected - reconnecting");
			/*
			 * we cannot just switch to APi every time when wifi fails, in most cases we should try to reconnect.
			 * although if the failure happened just after user configured wifi, there's a good chance
			 * that he made a mistake and we should switch to AP.
			 *
			 * TODO remember successful connection to wifi and do not fallback to AP if we ever managed connect to wifi.
			 */
			restore_wifi_setup();
			break;
		}

		case SYSTEM_EVENT_STA_GOT_IP: {
			ESP_LOGD(tag, "********************************************");
			ESP_LOGD(tag, "* Connected with WIFI network")
			ESP_LOGD(tag, "* Sensor IP address: " IPSTR, IP2STR(&event->event_info.got_ip.ip_info.ip));
			ESP_LOGD(tag, "********************************************");

			initialize_sntp();

			//g_mongooseStopRequest = 1; // Stop mongoose (if it is running).

			if (!g_mongooseStarted)
			{
				g_mongooseStarted = 1;
				xTaskCreatePinnedToCore(&mongooseTask, "bootwifi_mongoose_task", 10000, NULL, 5, NULL, 0);
			}

			break;
		}

		default:
			break;
	}

	return ESP_OK;
}

/**
 * Retrieve the connection info.  A rc==0 means ok.
 */
static int get_config(oc_wifi_t *oc_wifi) {
	memset(oc_wifi, 0, sizeof(oc_wifi_t));
	ESP_LOGD(tag, "retrieve wifi config");
	cJSON* wifi = storage_get_config("wifi");
	if (!wifi) return ESP_FAIL;
	cJSON* field;
	if ((field = cJSON_GetObjectItem(wifi, "ssid"))) strcpy(oc_wifi->ssid, field->valuestring);
	if ((field = cJSON_GetObjectItem(wifi, "password"))) strcpy(oc_wifi->password, field->valuestring);

	if ((field = cJSON_GetObjectItem(wifi, "ip"))) {
		inet_pton(AF_INET, field->valuestring, &oc_wifi->ipInfo.ip);
	}
	if ((field = cJSON_GetObjectItem(wifi, "gw"))) {
		inet_pton(AF_INET, field->valuestring, &oc_wifi->ipInfo.gw);
	}
	if ((field = cJSON_GetObjectItem(wifi, "netmask"))) {
		inet_pton(AF_INET, field->valuestring, &oc_wifi->ipInfo.netmask);
	}

	ESP_LOGD(tag, "wifi.ssid: %s", oc_wifi->ssid);
	ESP_LOGD(tag, "wifi.pass.lenght: [%d]", strlen(oc_wifi->password));

	ESP_LOGD(tag, "wifi.ip:" IPSTR, IP2STR(&oc_wifi->ipInfo.ip));
	ESP_LOGD(tag, "wifi.gateway:" IPSTR, IP2STR(&oc_wifi->ipInfo.gw));
	ESP_LOGD(tag, "wifi.netmask:" IPSTR, IP2STR(&oc_wifi->ipInfo.netmask));

	if (strlen(oc_wifi->ssid) == 0) {
		ESP_LOGW(tag, "NULL ssid detected");
		return ESP_FAIL;
	}

	return ESP_OK;
}

//static void saveConnectionInfo(oc_wifi_t *pConnectionInfo) {
//	storage_put_blob(KEY_CONNECTION_INFO, pConnectionInfo, sizeof(oc_wifi_t));
//}

static void become_station(oc_wifi_t *pConnectionInfo) {
	is_station = 1;
	ESP_LOGD(tag, "- Connecting to access point \"%s\" ...", pConnectionInfo->ssid);
	assert(strlen(pConnectionInfo->ssid) > 0);

	// If we have a static IP address information, use that.
	if (pConnectionInfo->ipInfo.ip.addr != 0) {
		ESP_LOGD(tag, " - using a static IP address of " IPSTR, IP2STR(&pConnectionInfo->ipInfo.ip));
		tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA);
		tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &pConnectionInfo->ipInfo);
	} else {
		tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_STA);
	}

  ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA));
  wifi_config_t sta_config;
  sta_config.sta.bssid_set = 0;
  memcpy(sta_config.sta.ssid, pConnectionInfo->ssid, SSID_SIZE);
  memcpy(sta_config.sta.password, pConnectionInfo->password, PASSWORD_SIZE);
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_connect());
}

static void become_access_point() {
	is_station = 0;
	ESP_LOGD(tag, "- Starting being an access point ...");
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

	wifi_config_t apConfig = {
		.ap = {
			.ssid="",
			.ssid_len=0,
			.password=CONFIG_OAP_AP_PASSWORD, //>= 8 chars
			.channel=0,
			.authmode=WIFI_AUTH_WPA_WPA2_PSK,
			.ssid_hidden=0,
			.max_connection=4,
			.beacon_interval=100
		}
	};

	//generate unique SSID
	uint8_t mac[6];
	esp_efuse_read_mac(mac);
	sprintf((char*)apConfig.ap.ssid, "OpenAirProject-%02X%02X", mac[0], mac[1]);

	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &apConfig));
	ESP_ERROR_CHECK(esp_wifi_start());
}

static int is_button_pressed() {
	return gpio_get_level(CONFIG_OAP_BTN_0_PIN);
}

static void restore_wifi_setup() {
	if (is_button_pressed()) {
		ESP_LOGI(tag, "GPIO override detected");
		become_access_point();
	} else {
		oc_wifi_t oc_wifi = {};
		int rc = get_config(&oc_wifi);
		if (rc == 0) {
			become_station(&oc_wifi);
		} else {
			become_access_point();
		}
	}
}

static void init_wifi() {
	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_init(esp32_wifi_eventHandler, NULL));
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
}

void bootWiFi() {
	ESP_LOGD(tag, ">> bootWiFi");

	gpio_pad_select_gpio(CONFIG_OAP_BTN_0_PIN);
	gpio_set_direction(CONFIG_OAP_BTN_0_PIN, GPIO_MODE_INPUT);
	gpio_set_pull_mode(CONFIG_OAP_BTN_0_PIN, GPIO_PULLDOWN_ONLY);

	init_wifi();
	restore_wifi_setup();

	ESP_LOGD(tag, "<< bootWiFi");
}
