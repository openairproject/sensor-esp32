/*
 * pm_meter.c
 *
 *  Created on: Feb 10, 2017
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <math.h>

#include "oap_common.h"
#include "pm_meter.h"
#include "pmsx003.h"

/*
 * PM Meter is used to perform periodic measurements. In that scenario,
 * pm sensor is enabled only for the time of measurement (this increases its lifetime and reduces power consumption).
 * however, due to nature of sensor construction, it requires some 'warming time' after switching on to ensure
 * proper air flow. Samples generated during 'warming period' are ignored. The rest of samples are accummmulated
 * in a buffer and used to caluclate average result at the end.
 */

static unsigned int sampleIndex;
static pm_data *sampleBuffer = NULL;
static long startedAt;
static int warmingTime;

static QueueHandle_t samples_queue;

static char* TAG = "pm_meter";

static void pm_data_print(char* str, pm_data pm) {
	ESP_LOGI(TAG, "%s pm1.0=%d pm2.5=%d pm10=%d", str, pm.pm1_0, pm.pm2_5, pm.pm10);
}

static void pm_data_collector_task() {
	pm_data pm;
	long localTime;
	while (1) {
		if (xQueueReceive(samples_queue, &pm, 100)) {
			pm_data* buf = sampleBuffer+(sampleIndex % CONFIG_OAP_PM_SAMPLE_BUF_SIZE);
			memcpy(buf, &pm, sizeof(pm));
			localTime = oap_epoch_sec();

			if (localTime - startedAt > 3600 * 24) {
				//localTime was set to proper value in a meantime. start over to avoid invalid warming time.
				startedAt = localTime;
			}

			if (localTime - startedAt > warmingTime) {
				sampleIndex++;
				pm_data_print("collect: ", *buf);
			} else {
				pm_data_print("warming: ", *buf);
			}
		}
	}
}

void pm_meter_start(unsigned int _warmingTime) {
	sampleIndex = 0;
	startedAt = oap_epoch_sec();
	warmingTime = _warmingTime;
	pms_enable(1);
}

pm_data pm_meter_sample(int disable_sensor) {
	if (disable_sensor) pms_enable(0);
	pm_data avg = {
		.pm1_0 = 0,
		.pm2_5 = 0,
		.pm10 = 0
	};
	unsigned int count = sampleIndex > CONFIG_OAP_PM_SAMPLE_BUF_SIZE ? CONFIG_OAP_PM_SAMPLE_BUF_SIZE : sampleIndex;
	if (!count) return avg;

	pm_data sample;
	for (unsigned int i = 0; i < count; i++) {
		sample = sampleBuffer[i];
		avg.pm1_0 += sample.pm1_0;
		avg.pm2_5 += sample.pm2_5;
		avg.pm10 += sample.pm10;
	}

	avg.pm1_0 /= count;
	avg.pm2_5 /= count;
	avg.pm10 /= count;

	ESP_LOGI(TAG, "stop measurement. recorded %d samples", count);
	pm_data_print("average: ", avg);

	return avg;
}

void pm_meter_init(QueueHandle_t _samples_queue) {
	samples_queue = _samples_queue;
	sampleBuffer = malloc(CONFIG_OAP_PM_SAMPLE_BUF_SIZE * sizeof(pm_data));
	xTaskCreate(pm_data_collector_task, "pm_data_collector_task", 1024*4, NULL, 10, NULL);
}
