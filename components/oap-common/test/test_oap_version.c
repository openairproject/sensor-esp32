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
#include "oap_version.h"

static void parse_format_verify(char* input, char* output) {
	oap_version_t ver;
	char* str;

	memset(&ver, 0, sizeof(ver));
	TEST_ESP_OK(oap_version_parse(input, &ver));
	str = oap_version_format(ver);
	TEST_ASSERT_EQUAL_STRING(output, str);
	free(str);
}

TEST_CASE("parse and format version","oap_version") {
	parse_format_verify("1.2.3","1.2.3");
	parse_format_verify("0.4.0","0.4.0");
	parse_format_verify("012.034.056","12.34.56");
}

TEST_CASE("version to num","oap_version") {
	oap_version_t v1 = {
		.major = 12,
		.minor = 34,
		.patch = 56,

	};
	TEST_ASSERT_EQUAL_UINT32(123456, oap_version_num(v1));

	oap_version_t v2 = {
		.major = 0,
		.minor = 4,
		.patch = 0,

	};
	TEST_ASSERT_EQUAL_UINT32(400, oap_version_num(v2));
}
