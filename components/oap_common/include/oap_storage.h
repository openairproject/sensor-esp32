/*
 * oap_storage.h
 *
 *  Created on: Feb 11, 2017
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

#ifndef COMPONENTS_OAP_COMMON_INCLUDE_OAP_STORAGE_H_
#define COMPONENTS_OAP_COMMON_INCLUDE_OAP_STORAGE_H_

#include <stdlib.h>
#include "cJSON.h"

/*
 * Achtung!
 * each time when you change structure of storable object, use different (versioned) key
 * to avoid situation when old blob is loaded into incompatible struct.
 *
 * TODO we could use json for storing settings, it is more 'tolerant' for such changes
 */
int storage_get_blob(const char* key, void* out_value, size_t length);
void storage_put_blob(const char* key, void* value, size_t length);

int storage_set_config_str(const char* config);
char* storage_get_config_str();

cJSON* storage_get_config(const char* module);

void storage_init();

#endif /* COMPONENTS_OAP_COMMON_INCLUDE_OAP_STORAGE_H_ */
