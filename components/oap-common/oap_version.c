/*
 * oap_version.c
 *
 *  Created on: Sep 7, 2017
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

#include "oap_version.h"
#include <stdio.h>
#include <stdlib.h>
#include "esp_err.h"

static oap_version_t _oap_version = { .major = OAP_VER_MAJOR, .minor = OAP_VER_MINOR, .patch = OAP_VER_PATCH };
static char* _oap_version_str = NULL;

static const char* VER_FORMAT="%d.%d.%d";

char* oap_version_format(oap_version_t ver) {
	char* str = malloc(snprintf( NULL, 0, VER_FORMAT, ver.major, ver.minor, ver.patch)+1);
	sprintf(str, VER_FORMAT, ver.major, ver.minor, ver.patch);
	return str;
}

oap_version_t oap_version() {
	return _oap_version;
}

char* oap_version_str() {
	if (!_oap_version_str) {
		_oap_version_str = oap_version_format(_oap_version);
	}
	return _oap_version_str;
}

unsigned long oap_version_num(oap_version_t ver) {
	return 10000 * ver.major + 100 * ver.minor + ver.patch;
}

esp_err_t oap_version_parse(char* str, oap_version_t* ver)
{
	int i = 0;
	while (str[i] != 0 && str[i] != '.') i++;
	if (str[i] != '.') return ESP_FAIL;
	int major = atoi(str);
	i++;
	int minor = atoi(str+i);
	while (str[i] != 0 && str[i] != '.') i++;
	if (str[i] != '.') return ESP_FAIL;
	i++;
	int patch = atoi(str+i);

	ver->major = major;
	ver->minor = minor;
	ver->patch = patch;

	return ESP_OK;
}


