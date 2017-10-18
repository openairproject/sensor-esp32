/*
 * version.h
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

#ifndef MAIN_INCLUDE_OAP_VERSION_H_
#define MAIN_INCLUDE_OAP_VERSION_H_

/**
 * http://semver.org/
 *
 * Given a version number MAJOR.MINOR.PATCH, increment the:
 *
 * MAJOR version when you make incompatible API changes,
 * MINOR version when you add functionality in a backwards-compatible manner, and
 * PATCH version when you make backwards-compatible bug fixes.
 */

#include <stdint.h>
#include "esp_err.h"

#define OAP_VER_MAJOR 1
#define OAP_VER_MINOR 2
#define OAP_VER_PATCH 0

typedef struct {
	uint8_t major;
	uint8_t minor;
	uint8_t patch;
} oap_version_t;

oap_version_t oap_version();
char* oap_version_str();
char* oap_version_format(oap_version_t ver);
esp_err_t oap_version_parse(char* str, oap_version_t* ver);
unsigned long oap_version_num(oap_version_t ver);

#endif /* MAIN_INCLUDE_OAP_VERSION_H_ */
