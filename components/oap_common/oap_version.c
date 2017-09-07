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

static oap_version_t oap_version = { .major = OAP_VER_MAJOR, .minor = OAP_VER_MINOR, .patch = OAP_VER_PATCH };
static char* _oap_version_str = NULL;

char* oap_version_str() {
	if (!_oap_version_str) {
		_oap_version_str = malloc(snprintf( NULL, 0, "%d.%d.%d", oap_version.major, oap_version.minor, oap_version.patch)+1);
		sprintf(_oap_version_str, "%d.%d.%d", oap_version.major, oap_version.minor, oap_version.patch);
	}
	return _oap_version_str;
}


