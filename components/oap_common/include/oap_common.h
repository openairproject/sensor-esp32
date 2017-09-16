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

#include "oap_version.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"

#define DEFAULT_TASK_PRIORITY (10)

typedef struct {
	uint16_t pm1_0;
	uint16_t pm2_5;
	uint16_t pm10;
	uint8_t sensor;
} pm_data;

typedef struct {
	double temp;
	double pressure;
	double humidity;
	uint8_t sensor;
} env_data;

typedef struct {
	pm_data* pm;
	pm_data* pm_aux;
	env_data* env;
	env_data* env_int;
	long int local_time;
} oap_meas;

typedef struct {
	int led;
	int heater;
	int fan;

	int indoor;
	int warmUpTime;
	int measTime;
	int measInterval;
	int measStrategy;
	int test;
} oap_sensor_config_t;

int is_reboot_in_progress();
void oap_reboot();

long oap_epoch_sec();
long oap_epoch_sec_valid();

//from esp-arduino

/**
 * creates a new string terminated with 0 from passed data.
 * free it after use.
 */
char* str_make(void* data, int len);

/*
 * allocates mem and copies src->dest
 */
char* str_dup(char* src);

//#define ESP_REG(addr) *((volatile uint32_t *)(addr))
#define NOP() asm volatile ("nop")

uint32_t micros();
uint32_t millis();
void delay(uint32_t);
void delayMicroseconds(uint32_t us);


#endif /* MAIN_COMMON_COMMON_H_ */
