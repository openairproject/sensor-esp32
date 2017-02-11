/**
 *  Created on: Nov 25, 2016
 *     Author: kolban
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
 *
 *
 * Bootwifi - Boot the WiFi environment.
 *
 * Compile with -DOAP_BTN_0_PIN=<num> where <num> is a GPIO pin number
 * to use a GPIO override.
 * See the README.md for full information.
 *
 */
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

extern const uint8_t select_wifi_html_start[] asm("_binary_select_wifi_html_start");
extern const uint8_t select_wifi_html_end[] asm("_binary_select_wifi_html_end");

#define KEY_CONNECTION_INFO "connectionInfo" // Key used in NVS for connection info
#define SSID_SIZE (32) // Maximum SSID size
#define PASSWORD_SIZE (64) // Maximum password size

typedef struct {
	char ssid[SSID_SIZE];
	char password[PASSWORD_SIZE];
	tcpip_adapter_ip_info_t ipInfo; // Optional static IP information
} oap_connection_info_t;

static bootwifi_callback_t g_callback = NULL; // Callback function to be invoked when we have finished.

static int g_mongooseStarted = 0; // Has the mongoose server started?
static int g_mongooseStopRequest = 0; // Request to stop the mongoose server.

// Forward declarations
static void saveConnectionInfo(oap_connection_info_t *pConnectionInfo);
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

/**
 * Handle mongoose events.  These are mostly requests to process incoming
 * browser requests.  The ones we handle are:
 * GET / - Send the enter details page.
 * GET /set - Set the connection info (REST request).
 * POST /ssidSelected - Set the connection info (HTML FORM).
 */
static void mongoose_event_handler(struct mg_connection *nc, int ev, void *evData) {
	ESP_LOGV(tag, "- Event: %s", mongoose_eventToString(ev));
	switch (ev) {
		case MG_EV_HTTP_REQUEST: {
			struct http_message *message = (struct http_message *) evData;
			char *uri = mgStrToStr(message->uri);
			ESP_LOGD(tag, " - uri: %s", uri);

			if (strcmp(uri, "/set") ==0 ) {
				oap_connection_info_t connectionInfo;
//fix
				saveConnectionInfo(&connectionInfo);
				ESP_LOGD(tag, "- Set the new connection info to ssid: %s, password: %s",
					connectionInfo.ssid, connectionInfo.password);
				mg_send_head(nc, 200, 0, "Content-Type: text/plain");
			} if (strcmp(uri, "/") == 0) {
				size_t resp_size = select_wifi_html_end-select_wifi_html_start;

				ESP_LOGI(tag, "size %d, start: %d, end:%d", resp_size, (int)select_wifi_html_start, (int)select_wifi_html_end);

				mg_send_head(nc, 200, resp_size, "Content-Type: text/html");
				mg_send(nc, select_wifi_html_start, resp_size);
			}
			// Handle /ssidSelected
			// This is an incoming form with properties:
			// * ssid - The ssid of the network to connect against.
			// * password - the password to use to connect.
			// * ip - Static IP address ... may be empty
			// * gw - Static GW address ... may be empty
			// * netmask - Static netmask ... may be empty
			if(strcmp(uri, "/ssidSelected") == 0) {
				// We have received a form page containing the details.  The form body will
				// contain:
				// ssid=<value>&password=<value>
				ESP_LOGD(tag, "- body: %.*s", message->body.len, message->body.p);
				oap_connection_info_t connectionInfo;
				mg_get_http_var(&message->body, "ssid",	connectionInfo.ssid, SSID_SIZE);
				mg_get_http_var(&message->body, "password", connectionInfo.password, PASSWORD_SIZE);

				char ipBuf[20];
				if (mg_get_http_var(&message->body, "ip", ipBuf, sizeof(ipBuf)) > 0) {
					inet_pton(AF_INET, ipBuf, &connectionInfo.ipInfo.ip);
				} else {
					connectionInfo.ipInfo.ip.addr = 0;
				}

				if (mg_get_http_var(&message->body, "gw", ipBuf, sizeof(ipBuf)) > 0) {
					inet_pton(AF_INET, ipBuf, &connectionInfo.ipInfo.gw);
				}
				else {
					connectionInfo.ipInfo.gw.addr = 0;
				}

				if (mg_get_http_var(&message->body, "netmask", ipBuf, sizeof(ipBuf)) > 0) {
					inet_pton(AF_INET, ipBuf, &connectionInfo.ipInfo.netmask);
				}
				else {
					connectionInfo.ipInfo.netmask.addr = 0;
				}

				ESP_LOGD(tag, "ssid: %s, password: %s", connectionInfo.ssid, connectionInfo.password);

				mg_send_head(nc, 200, 0, "Content-Type: text/plain");
				saveConnectionInfo(&connectionInfo);
				restore_wifi_setup();
			}
			else {
				mg_send_head(nc, 404, 0, "Content-Type: text/plain");
			}
			nc->flags |= MG_F_SEND_AND_CLOSE;
			free(uri);
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

	// Since we HAVE ended mongoose, time to invoke the callback.
	if (g_callback) {
		g_callback(1);
	}

	ESP_LOGD(tag, "<< mongooseTask");
	vTaskDelete(NULL);
	return;
} // mongooseTask


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
			tcpip_adapter_ip_info_t ip_info;
			tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info);

			//192.168.4.1 this is the default IP of all esp devices
			//http://www.esp8266.com/viewtopic.php?f=29&t=12124
			/*
from arduino-esp32
bool WiFiAPClass::softAPConfig(IPAddress local_ip, IPAddress gateway, IPAddress subnet)
{

    if(!WiFi.enableAP(true)) {
        // enable AP failed
        return false;
    }

    tcpip_adapter_ip_info_t info;
    info.ip.addr = static_cast<uint32_t>(local_ip);
    info.gw.addr = static_cast<uint32_t>(gateway);
    info.netmask.addr = static_cast<uint32_t>(subnet);
    tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP);
    if(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &info)) {
        return tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP);
    }
    return false;
}

			 */

			ESP_LOGI(tag, "**********************************************");
			ESP_LOGI(tag, "* We are now an access point and you can point")
			ESP_LOGI(tag, "* your browser to http://" IPSTR, IP2STR(&ip_info.ip));
			ESP_LOGI(tag, "**********************************************");
			// Start Mongoose ...
			if (!g_mongooseStarted)
			{
				g_mongooseStarted = 1;
				xTaskCreatePinnedToCore(&mongooseTask, "bootwifi_mongoose_task", 8000, NULL, 5, NULL, 0);
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
			 *
			 */
			//become_access_point();
			restore_wifi_setup();
			break;
		} // SYSTEM_EVENT_AP_START

		// If we connected as a station then we are done and we can stop being a
		// web server.
		case SYSTEM_EVENT_STA_GOT_IP: {
			ESP_LOGD(tag, "********************************************");
			ESP_LOGD(tag, "* We are now connected and ready to do work!")
			ESP_LOGD(tag, "* - Our IP address is: " IPSTR, IP2STR(&event->event_info.got_ip.ip_info.ip));
			ESP_LOGD(tag, "********************************************");

			initialize_sntp();

			g_mongooseStopRequest = 1; // Stop mongoose (if it is running).
			// Invoke the callback if Mongoose has NOT been started ... otherwise
			// we will invoke the callback when mongoose has ended.
			if (!g_mongooseStarted) {
				if (g_callback) {
					g_callback(1);
				}
			} // Mongoose was NOT started
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
static int getConnectionInfo(oap_connection_info_t *pConnectionInfo) {
	size_t size = sizeof(oap_connection_info_t);
	if (storage_get_blob(KEY_CONNECTION_INFO, pConnectionInfo, size) != ESP_OK) {
		return -1;
	}
	if (strlen(pConnectionInfo->ssid) == 0) {
		ESP_LOGW(tag, "NULL ssid detected");
		return -1;
	}
	return ESP_OK;
}

static void saveConnectionInfo(oap_connection_info_t *pConnectionInfo) {
	storage_put_blob(KEY_CONNECTION_INFO, pConnectionInfo, sizeof(oap_connection_info_t));
}

static void become_station(oap_connection_info_t *pConnectionInfo) {
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
	ESP_LOGD(tag, "- Starting being an access point ...");
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

	wifi_config_t apConfig = {
		.ap = {
			.ssid="",
			.ssid_len=0,
			.password="cleanair", //>= 8 chars
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
		oap_connection_info_t connectionInfo;
		int rc = getConnectionInfo(&connectionInfo);
		if (rc == 0) {
			become_station(&connectionInfo);
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

/**
 * Main entry into bootWiFi
 */
void bootWiFi(bootwifi_callback_t callback) {
	ESP_LOGD(tag, ">> bootWiFi");

	g_callback = callback;

	gpio_pad_select_gpio(CONFIG_OAP_BTN_0_PIN);
	gpio_set_direction(CONFIG_OAP_BTN_0_PIN, GPIO_MODE_INPUT);
	gpio_set_pull_mode(CONFIG_OAP_BTN_0_PIN, GPIO_PULLDOWN_ONLY);

	init_wifi();
	restore_wifi_setup();

	ESP_LOGD(tag, "<< bootWiFi");
} // bootWiFi
