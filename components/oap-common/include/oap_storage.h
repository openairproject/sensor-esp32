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


/**
 * @brief    initialise storage and config
 */
void storage_init();

/**
 * @brief    return config for given submodule
 *
 * @param[in]  module  name of a submodule, e.g. 'wifi' or NULL to retrieve full config tree
 *
 * @return	config JSON. DO NOT free or modify the result, it is a singleton!
 */
cJSON* storage_get_config(const char* module);


/**
 * @brief returns config json as a string
 *
 * sensitive data (wifi password) is replaced with constant string
 *
 * @return config json. free it after use!
 */
cJSON* storage_get_config_to_update();


/**
 * @bried updates json config
 *
 * sensitive data (wifi password) that has not been changed is being replaced with proper values.
 *
 * @param[in] json config
 */
void storage_update_config(cJSON* config);


#endif /* COMPONENTS_OAP_COMMON_INCLUDE_OAP_STORAGE_H_ */
