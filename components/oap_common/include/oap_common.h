/*
 * common.h
 *
 *  Created on: Feb 9, 2017
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

#ifndef MAIN_COMMON_COMMON_H_
#define MAIN_COMMON_COMMON_H_

typedef struct {
	unsigned int pm1_0;
	unsigned int pm2_5;
	unsigned int pm10;
} pm_data;

typedef struct {
	double temp;
	double pressure;
} env_data;

typedef struct {
	pm_data pm;
	env_data env;
	long int local_time;
} oap_meas;

#endif /* MAIN_COMMON_COMMON_H_ */
