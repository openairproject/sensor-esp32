/*
 * test_meas.c
 *
 *  Created on: Sep 24, 2017
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


#include "pm_meter.h"
#include "meas_intervals.h"
#include "oap_test.h"
#include "freertos/task.h"

extern pm_meter_t pm_meter_intervals;

static int fake_sensor0_enabled = 0;
static int fake_sensor1_enabled = 0;
static list_t* strategy_events = NULL;

#define TAG "test"

typedef struct {
	pm_meter_event_t event_type;
	char* error;
	pm_data_pair_t* result;
} test_meas_record_t;

static void strategy_callback(pm_meter_event_t event, void* data) {
	test_meas_record_t* record = malloc(sizeof(test_meas_record_t));
	memset(record, 0, sizeof(test_meas_record_t));

	record->event_type = event;
	switch (event) {
		case PM_METER_START:
			ESP_LOGI(TAG, "MEAS_START");
			break;
		case PM_METER_RESULT :
			ESP_LOGI(TAG, "MEAS_RESULT");
			record->result = malloc(sizeof(pm_data_pair_t));
			memcpy(record->result, data, sizeof(pm_data_pair_t));
			break;
		case PM_METER_ERROR:
			ESP_LOGI(TAG, "MEAS_ERROR: %s", (char*)data);
			record->error = strdup((char*)data);
			break;
	}
	list_insert(strategy_events, record);
}


static void sensor_handler0(uint8_t enable) {
	fake_sensor0_enabled = enable;
}
static void sensor_handler1(uint8_t enable) {
	fake_sensor1_enabled = enable;
}

static pm_sensor_enable_handler_pair_t pm_sensor_handler_pair;
static pm_meter_intervals_params_t strategy_test_params;

static TaskHandle_t fake_sensor_task_handle = NULL;

static void fake_sensor() {
	pm_data_t sample0 = {
		.pm1_0 = 0,
		.pm2_5 = 0,
		.pm10 = 0,
		.sensor_idx = 0
	};

	pm_data_t sample1 = {
		.pm1_0 = 1,
		.pm2_5 = 2,
		.pm10 = 3,
		.sensor_idx = 1
	};

	while (1) {
		if (fake_sensor0_enabled) {
			pm_meter_intervals.input(&sample0);
			sample0.pm1_0++;
			sample0.pm2_5+=2;
			sample0.pm10+=4;
		}
		if (fake_sensor1_enabled) {
			pm_meter_intervals.input(&sample1);
		}
		delay(100);
	}
}

static int has_result(void* data) {
	return data && ((test_meas_record_t*)data)->event_type == PM_METER_RESULT;
}
static int free_record(test_meas_record_t* record) {
	if (record->error) free(record->error);
	if (record->result) free(record->result);
	free(record);
	return 0;
}

TEST_CASE("meas intervals","[meas]") {

	//configure
	pm_sensor_handler_pair.count = 2;
	pm_sensor_handler_pair.handler[0] = &sensor_handler0;
	pm_sensor_handler_pair.handler[1] = &sensor_handler1;

	strategy_test_params.warm_up_time = 1; //one sec warming
	strategy_test_params.meas_time = 3,	 //two sec collect
	strategy_test_params.meas_interval = 10;

	strategy_events = list_createList();
	xTaskCreate(fake_sensor, "fake_sensor", 2*1024, NULL, DEFAULT_TASK_PRIORITY, &fake_sensor_task_handle);

	pm_meter_intervals.start(&pm_sensor_handler_pair, &strategy_test_params, strategy_callback);
	list_t* result_record;

	//wait a bit longer for a result
	test_timer_t timer = {.started = 0, .wait_for = strategy_test_params.meas_time * 1000 + 1000};
	while ((result_record = list_find(strategy_events, &has_result)) == NULL && !test_timeout(&timer)) {
		delay(50);
	}

	//cleanup
	pm_meter_intervals.stop();
	vTaskDelete(fake_sensor_task_handle);

	//check
	if (!result_record) {
		list_deleteListAndValues(strategy_events, &free_record);
		TEST_FAIL_MESSAGE("timeout while waiting for measurement result_record");
	} else {
		pm_data_pair_t* meas_data = ((test_meas_record_t*)(result_record->value))->result;
		//sensor 0
		TEST_ASSERT_EQUAL_INT8(0, meas_data->pm_data[0].sensor_idx);
		TEST_ASSERT_MESSAGE(meas_data->pm_data[0].pm1_0 >= 19 && meas_data->pm_data[0].pm1_0 <= 21, "invalid 0->pm1.0 avg");
		TEST_ASSERT_MESSAGE(meas_data->pm_data[0].pm2_5 >= 38 && meas_data->pm_data[0].pm2_5 <= 42, "invalid 0->pm2.5 avg");
		TEST_ASSERT_MESSAGE(meas_data->pm_data[0].pm10 >= 76 && meas_data->pm_data[0].pm10 <= 84, "invalid 0->pm10 avg");
		TEST_ASSERT_FALSE_MESSAGE(fake_sensor0_enabled, "sensor0 not disabled after measurement");

		//sensor 1
		TEST_ASSERT_EQUAL_INT8(1, meas_data->pm_data[1].sensor_idx);
		TEST_ASSERT_EQUAL_INT16(1, meas_data->pm_data[1].pm1_0);
		TEST_ASSERT_EQUAL_INT16(2, meas_data->pm_data[1].pm2_5);
		TEST_ASSERT_EQUAL_INT16(3, meas_data->pm_data[1].pm10);
		TEST_ASSERT_FALSE_MESSAGE(fake_sensor1_enabled, "sensor1 not disabled after measurement");
		list_deleteListAndValues(strategy_events, &free_record);
	}

}
