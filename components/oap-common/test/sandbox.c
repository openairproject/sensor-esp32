/*
 * sandbox.c
 *
 *  Created on: Sep 12, 2017
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

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include "unity.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_partition.h"
#include "esp_log.h"
#include <string.h>
#include "esp_system.h"

/*
typedef struct {
	uint8_t num;
	char* str;
} sample_struct;

static const char* TAG = "test";

void* modify_struct_by_value(sample_struct ss) {
	ss.num++;
	ss.str = "modified!";
	//warning: function returns address of local variable [-Wreturn-local-addr]
	return &ss;
}

void modify_struct_by_ref(sample_struct* ss) {
	ss->num++;
	ss->str = "modified!"; //what happens with assigned string? tricky - if it was a const we cannot release it. prefer immutable structs!
}

TEST_CASE("structs","sandbox") {
	sample_struct ss = {
			.num = 1,
			.str = "hello"
	};

	void* ssp = modify_struct_by_value(ss);

	TEST_ASSERT_EQUAL_STRING("hello", ss.str);
	TEST_ASSERT_EQUAL_INT(1, ss.num);
	TEST_ASSERT_EQUAL_INT32(0, ssp);

	char* str = ss.str;
	modify_struct_by_ref(&ss);
	TEST_ASSERT_EQUAL_STRING("modified!", ss.str);
	TEST_ASSERT_EQUAL_INT(2, ss.num);

	ESP_LOGI(TAG, "old str:%s", str);
	//str[0] = '!'; //error (const allocation)
	//free(str); //error free() target pointer is outside heap areas
}*/

/*
TEST_CASE("mac", "sandbox") {
	uint8_t mac[6];
	esp_efuse_mac_get_default(mac);
	ESP_LOGD("TST", "MAC= %02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}
*/
