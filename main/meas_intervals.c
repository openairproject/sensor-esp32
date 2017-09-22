/*
 * meas_intervals.c
 *
 *  Created on: Mar 25, 2017
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

#include "meas_intervals.h"
#include "esp_log.h"
#include "oap_debug.h"

static char* TAG = "meas_int";

typedef struct {
	pms_config_t config;
	uint16_t sample_count;
	pm_data* samples;
} sensor_model_t;

sensor_model_t* sensors;
uint8_t sensor_count;
meas_strategy_callback _callback;
meas_intervals_params_t _params;

static long startedAt;

static void pm_data_print(char* str, uint8_t sensor, pm_data* pm) {
	ESP_LOGI(TAG, "%s[%d] pm1.0=%d pm2.5=%d pm10=%d", str, sensor, pm->pm1_0, pm->pm2_5, pm->pm10);
}

static void collect(pm_data* pm) {
	long localTime;
	sensor_model_t* sensor = sensors+pm->sensor;

	pm_data* buf = sensor->samples+(sensor->sample_count % CONFIG_OAP_PM_SAMPLE_BUF_SIZE);
	memcpy(buf, pm, sizeof(pm_data));
	localTime = oap_epoch_sec();

	if (localTime - startedAt > 3600 * 24) {
		//localTime was set to proper value in a meantime. start over to avoid invalid warming time.
		startedAt = localTime;
	}

	if (localTime - startedAt > _params.warmUpTime) {
		sensor->sample_count++;
		pm_data_print("collect", pm->sensor, buf);
	} else {
		pm_data_print("warming", pm->sensor, buf);
	}
}

static void init_sensors() {
	ESP_LOGI(TAG, "init_sensors");

	for (uint8_t c = 0; c < sensor_count; c++) {
		pms_init(&sensors[c].config);
	}
}

static void enable_sensors() {
	ESP_LOGI(TAG, "enable_sensors");

	for (uint8_t s = 0; s < sensor_count; s++) {
		sensors[s].sample_count = 0;
		pms_enable(&sensors[s].config, 1);
	}
}

static esp_err_t pm_meter_sample(uint8_t s, pm_data* result, uint8_t disable_sensor) {
	sensor_model_t* sensor = sensors+s;

	if (disable_sensor) {
		pms_enable(&sensor->config, 0);
	}

	uint16_t count = sensor->sample_count > CONFIG_OAP_PM_SAMPLE_BUF_SIZE ? CONFIG_OAP_PM_SAMPLE_BUF_SIZE : sensor->sample_count;
	if (count == 0) {
		ESP_LOGW(TAG, "no samples recorded for sensor %d", s);
		return ESP_FAIL;
	}

	memset(result, 0, sizeof(pm_data));
	pm_data* sample;
	for (uint16_t i = 0; i < count; i++) {
		sample = sensor->samples+i;
		result->pm1_0 += sample->pm1_0;
		result->pm2_5 += sample->pm2_5;
		result->pm10 += sample->pm10;
	}

	result->pm1_0 /= count;
	result->pm2_5 /= count;
	result->pm10 /= count;
	result->sensor = s;

	ESP_LOGI(TAG, "stop measurement for sensor %d, recorded %d samples", s, count);
	pm_data_print("average", s, result);

	return ESP_OK;
}

static void task() {
	init_sensors();

	int delay_sec = (_params.measInterval-_params.measTime);
	while (1) {
		log_task_stack("pm_meter_trigger");

		_callback(MEAS_START,NULL);

		startedAt = oap_epoch_sec();

		enable_sensors();

		delay(_params.measTime * 1000);

		pm_data_duo_t pm_data_duo = {
			.count = sensor_count
		};

		uint8_t ok = 1;
		for (uint8_t s = 0; ok && s < sensor_count; s++) {
			ESP_LOGI(TAG, "compute results for sensor %d", s);
			if (pm_meter_sample(s, pm_data_duo.data+s, delay_sec>0) != ESP_OK) {
				_callback(MEAS_ERROR, "cannot calculate result");
				ok = 0;
			}
		}

		if (ok) {
			_callback(MEAS_RESULT,&pm_data_duo);
		}

		if (delay_sec > 0) {
			delay(delay_sec * 1000);
		} else {
			delay(10);
		}
	}
}

static void start(pms_configs_t* pms_configs, meas_intervals_params_t* params, meas_strategy_callback callback) {
	ESP_LOGI(TAG, "start");
	_callback = callback;
	memcpy(&_params, params, sizeof(meas_intervals_params_t));

	sensor_count = pms_configs->count;
	sensors = malloc(sizeof(sensor_model_t)*sensor_count);
	for (uint8_t c = 0; c < sensor_count; c++) {
		sensor_model_t* sensor = sensors+c;
		memset(sensor, 0, sizeof(sensor_model_t));
		memcpy(&sensor->config, pms_configs->sensor[c], sizeof(pms_config_t));
		sensor->samples = malloc(sizeof(pm_data)*CONFIG_OAP_PM_SAMPLE_BUF_SIZE);
	}
	xTaskCreate(task, "meas_intervals", 1024*4, NULL, DEFAULT_TASK_PRIORITY, NULL);
}

meas_strategy_t meas_intervals = {
	.collect = &collect,
	.start = &start
};
