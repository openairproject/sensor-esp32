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
#include "oap_common.h"
#include "oap_debug.h"
#include "esp_err.h"
#include "freertos/task.h"

static char* TAG = "meter_intv";

typedef struct {
	pm_sensor_enable_handler_f handler;
	uint16_t sample_count;
	pm_data_t* samples;
} sensor_model_t;

static sensor_model_t* sensors;
static uint8_t sensor_count;
static pm_meter_output_f _callback;
static pm_meter_intervals_params_t _params;

static long startedAt;

static void pm_data_print(char* str, uint8_t sensor, pm_data_t* pm) {
	ESP_LOGI(TAG, "%s[%d] pm1.0=%d pm2.5=%d pm10=%d", str, sensor, pm->pm1_0, pm->pm2_5, pm->pm10);
}

static void collect(pm_data_t* pm) {
	sensor_model_t* sensor = sensors+pm->sensor_idx;

	pm_data_t* buf = sensor->samples+(sensor->sample_count % CONFIG_OAP_PM_SAMPLE_BUF_SIZE);
	memcpy(buf, pm, sizeof(pm_data_t));

	if (millis() - startedAt > _params.warm_up_time * 1000) {
		sensor->sample_count++;
		pm_data_print("collect", pm->sensor_idx, buf);
	} else {
		pm_data_print("warming", pm->sensor_idx, buf);
	}
}

static void enable_sensors() {
	ESP_LOGI(TAG, "enable_sensors");

	for (uint8_t s = 0; s < sensor_count; s++) {
		sensors[s].sample_count = 0;
		sensors[s].handler(1);
	}
}

static esp_err_t pm_meter_sample(uint8_t s, pm_data_t* result, uint8_t disable_sensor) {
	sensor_model_t* sensor = sensors+s;

	if (disable_sensor) {
		sensor->handler(0);
	}

	uint16_t count = sensor->sample_count > CONFIG_OAP_PM_SAMPLE_BUF_SIZE ? CONFIG_OAP_PM_SAMPLE_BUF_SIZE : sensor->sample_count;
	if (count == 0) {
		ESP_LOGW(TAG, "no samples recorded for sensor %d", s);
		return ESP_FAIL;
	}

	memset(result, 0, sizeof(pm_data_t));
	pm_data_t* sample;
	for (uint16_t i = 0; i < count; i++) {
		sample = sensor->samples+i;
		result->pm1_0 += sample->pm1_0;
		result->pm2_5 += sample->pm2_5;
		result->pm10 += sample->pm10;
	}

	result->pm1_0 /= count;
	result->pm2_5 /= count;
	result->pm10 /= count;
	result->sensor_idx = s;

	ESP_LOGI(TAG, "stop measurement for sensor %d, recorded %d samples", s, count);
	pm_data_print("average", s, result);

	return ESP_OK;
}

static void task() {
	int delay_sec = (_params.meas_interval-_params.meas_time);
	while (1) {
		log_task_stack("pm_meter_trigger");

		_callback(PM_METER_START,NULL);

		startedAt = millis();

		enable_sensors();

		delay(_params.meas_time * 1000);

		pm_data_pair_t pm_data_pair = {
			.count = sensor_count
		};

		uint8_t ok = 1;
		for (uint8_t s = 0; ok && s < sensor_count; s++) {
			ESP_LOGI(TAG, "compute results for sensor %d", s);
			if (pm_meter_sample(s, pm_data_pair.pm_data+s, delay_sec>0) != ESP_OK) {
				_callback(PM_METER_ERROR, "cannot calculate result");
				ok = 0;
			}
		}

		if (ok) {
			pm_data_pair.timestamp = oap_epoch_sec();
			_callback(PM_METER_RESULT,&pm_data_pair);
		}

		if (delay_sec > 0) {
			delay(delay_sec * 1000);
		} else {
			delay(10);
		}
	}
}

static TaskHandle_t task_handle = NULL;
static void start(pm_sensor_enable_handler_pair_t* pm_sensor_handlers, pm_meter_intervals_params_t* params, pm_meter_output_f callback) {
	ESP_LOGI(TAG, "start");
	_callback = callback;
	memcpy(&_params, params, sizeof(pm_meter_intervals_params_t));

	sensor_count = pm_sensor_handlers->count;
	sensors = malloc(sizeof(sensor_model_t)*sensor_count);
	for (uint8_t c = 0; c < sensor_count; c++) {
		sensor_model_t* sensor = sensors+c;
		memset(sensor, 0, sizeof(sensor_model_t));
		sensor->handler = pm_sensor_handlers->handler[c];
		sensor->samples = malloc(sizeof(pm_data_t)*CONFIG_OAP_PM_SAMPLE_BUF_SIZE);
	}
	xTaskCreate(task, "pm_meter_intervals", 1024*4, NULL, DEFAULT_TASK_PRIORITY, &task_handle);
}

static void stop() {
	if (task_handle) {
		vTaskDelete(task_handle);
		task_handle = NULL;
	}
}

pm_meter_t pm_meter_intervals = {
	.input = &collect,
	.start = (pm_meter_start_f)&start,
	.stop = &stop
};
