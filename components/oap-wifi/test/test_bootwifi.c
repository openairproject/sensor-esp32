/*
 * oap_test.c
 *
 *  Created on: Sep 11, 2017
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

#include "oap_test.h"
#include "bootwifi.h"
#include "oap_storage.h"
#include "esp_wifi.h"
#include "server_cpanel.h"
#include "esp_request.h"


TEST_CASE("test wifi STA","[wifi]") {
	test_require_wifi();
}

/*
 * this test is problematic because esp32 encounters issues after switching back from AP to STA.
 * sometimes after this transition, a few ssl requests fail and then it recovers (cache?)
 * run it as a second to last one, and as last - do 'reconnect wifi'
 */
TEST_CASE("test wifi AP","[wifi]") {
	//TEST_IGNORE();
	test_require_ap();
}

//TEST_CASE("test cpanel in AP","[wifi]") {
//	test_require_ap_with(cpanel_wifi_handler);
//
//	test_delay(1000);
//	request_t* r = req_new("http://www.google.com"); //this breaks mongoose socket
//	req_perform(r);
//	req_clean(r);
//
//	test_delay(10000);
//}

// this test is unstable - sometimes it reconnects before we're able to detect disconnect
//
//TEST_CASE("reconnect wifi","[wifi]") {
//	test_require_wifi();
//	esp_wifi_stop();
//	TEST_ESP_OK(wifi_disconnected_wait_for(5000));
//	TEST_ESP_OK(wifi_connected_wait_for(10000));
//}
