/*
 * bootwifi.h
 *
 *  Created on: Nov 25, 2016
 *      Author: kolban
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

#ifndef MAIN_BOOTWIFI_H_
#define MAIN_BOOTWIFI_H_

#include <tcpip_adapter.h>
#include "cJSON.h"

#define SSID_SIZE (32) // Maximum SSID size
#define PASSWORD_SIZE (64) // Maximum password size

typedef void(*wifi_state_callback_f)(bool connected, bool ap_mode);

typedef struct {
	char ssid[SSID_SIZE];
	char password[PASSWORD_SIZE];
	tcpip_adapter_ip_info_t ipInfo; // Optional static IP information
	int ap_mode;
	int control_panel;
	wifi_state_callback_f callback;
} oc_wifi_t;



esp_err_t wifi_configure(cJSON* wifi, wifi_state_callback_f wifi_state_callback);
void wifi_boot();
esp_err_t wifi_connected_wait_for(uint32_t ms);
esp_err_t wifi_ap_started_wait_for(uint32_t ms);
esp_err_t wifi_disconnected_wait_for(uint32_t ms);



#endif /* MAIN_BOOTWIFI_H_ */
