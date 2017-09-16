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

#define SSID_SIZE (32) // Maximum SSID size
#define PASSWORD_SIZE (64) // Maximum password size


typedef struct {
	char ssid[SSID_SIZE];
	char password[PASSWORD_SIZE];
	tcpip_adapter_ip_info_t ipInfo; // Optional static IP information
} oc_wifi_t;

void wifi_boot(oc_wifi_t* wifi_config, uint8_t enable_control_panel);
esp_err_t wifi_connected_wait();
esp_err_t wifi_connected_wait_for(uint32_t ms);

#endif /* MAIN_BOOTWIFI_H_ */
