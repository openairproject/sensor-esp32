/*
 * oap_common.c
 *
 *  Created on: Feb 22, 2017
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


#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include <sys/time.h>
#include "oap_common.h"

static const long FEB22_2017 = 1487795557;

long oap_epoch_sec() {
	struct timeval tv_start;
	gettimeofday(&tv_start, NULL);
	return tv_start.tv_sec;
}

long oap_epoch_sec_valid() {
	long epoch = oap_epoch_sec();
	return epoch > FEB22_2017 ? epoch : 0;
}
