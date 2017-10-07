/*
 * main.c
 *
 *  Created on: Feb 3, 2017
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
#include "freertos/semphr.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <math.h>

#include "../components/thing-speak/include/thing_speak.h"
#include "meas_intervals.h"
#include "meas_continuous.h"

#include "bootwifi.h"
#include "rgb_led.h"
#include "ctrl_btn.h"
#include "bmx280.h"
#include "pmsx003.h"
#include "oap_common.h"
#include "oap_storage.h"
#include "oap_debug.h"
#include "awsiot.h"
#include "ota.h"
#include "oap_data.h"
#include "server_cpanel.h"

static const char *TAG = "app";


static oap_sensor_config_t oap_sensor_config;



/** -- LED -----------------------------------------
 *
 *  We set led state asynchronously, via a queue.
 *
 */

static QueueHandle_t ledc_queue;

static led_cmd_t ledc_state = {
	.color = {.v = {0,0,1}} //initial color (when no samples collected)
};

static void ledc_set() {
	xQueueSend(ledc_queue, &ledc_state, 100);
}

static void ledc_set_mode(led_mode_t mode) {
	ledc_state.mode = mode;
	ledc_set();
}

static void ledc_set_color(led_mode_t mode, float r, float g, float b) {
	ledc_state.color.v[0] = r;
	ledc_state.color.v[1] = g;
	ledc_state.color.v[2] = b;
	ledc_set_mode(mode);
}

static void ledc_init() {
	ledc_queue = xQueueCreate(10, sizeof(led_cmd_t));
	led_init(oap_sensor_config.led, ledc_queue);
	ledc_set();
}


/** -- PM METER -------------------------------------
 *
 *  We do not control PM sensors directly, but via PM meter.
 *  Type of PM meter determines the way we perform measurement.
 *  Normally, it happens periodically by collecting number of samples
 *  (with initial warming period when samples are ignored).
 *
 *  Single result from PM meter is then put into a queue from where
 *  it can be propagated to publishers (awsiot, thingspeak etc.)
 */

static QueueHandle_t pm_meter_result_queue;
static pmsx003_config_t pms_pair_config[2];
extern pm_meter_t pm_meter_intervals;
extern pm_meter_t pm_meter_continuous;

#define PM_RESULT_SEND_TIMEOUT 100

static void pm_meter_result_handler(pm_data_pair_t* pm_data_pair) {
	if (!xQueueSend(pm_meter_result_queue, pm_data_pair, PM_RESULT_SEND_TIMEOUT)) {
		ESP_LOGW(TAG,"pm_meter_result_queue overflow");
	}
}

static void pm_meter_output_handler(pm_meter_event_t event, void* data) {
	switch (event) {
	case PM_METER_START:
		ESP_LOGI(TAG, "start measurement");
		ledc_set_mode(LED_PULSE);
		break;
	case PM_METER_RESULT:
		ESP_LOGI(TAG, "finished measurement");
		pm_meter_result_handler((pm_data_pair_t*)data);
		break;
	case PM_METER_ERROR:
		ESP_LOGW(TAG, "failed measurement: %s", (char*)data);
		break;
	}
}

//TODO share
static void set_gpio(uint8_t gpio, uint8_t enabled) {
	if (gpio > 0) {
		ESP_LOGD(TAG, "set pin %d => %d", gpio, enabled);
		gpio_set_level(gpio, enabled);
	}
}

//TODO share
static void configure_gpio(uint8_t gpio) {
	if (gpio > 0) {
		ESP_LOGD(TAG, "configure pin %d as output", gpio);
		gpio_pad_select_gpio(gpio);
		ESP_ERROR_CHECK(gpio_set_direction(gpio, GPIO_MODE_OUTPUT));
		ESP_ERROR_CHECK(gpio_set_pull_mode(gpio, GPIO_PULLDOWN_ONLY));
	}
}

static void pmsx003_enable_0(uint8_t enable) {
	if (oap_sensor_config.heater) {
		set_gpio(pm_meter_aux.heater_pin, enable);
	}
	if (oap_sensor_config.fan) {
		set_gpio(pm_meter_aux.fan_pin, enable);
	}
	pmsx003_enable(&pms_pair_config[0], enable);
}

static void pmsx003_enable_1(uint8_t enable) {
	pmsx003_enable(&pms_pair_config[1], enable);
}

static int pmsx003_configure(pm_data_callback_f callback) {
	memset(pms_pair_config, 0, sizeof(pmsx003_config_t)*2);

	pmsx003_set_hardware_config(&pms_pair_config[0], 0);

	pms_pair_config[0].indoor = oap_sensor_config.indoor;
	pms_pair_config[0].callback = callback;

	if (pmsx003_set_hardware_config(&pms_pair_config[1], 1) == ESP_OK) {
		pms_pair_config[1].indoor = oap_sensor_config.indoor;
		pms_pair_config[1].callback = callback;
		return 2;
	} else {
		return 1;
	}
}

static esp_err_t pm_meter_init() {
	pm_meter_t* pm_meter;
	void* params = NULL;

	if (oap_sensor_config.fan) {
		configure_gpio(pm_meter_aux.fan_pin);
	}
	if (oap_sensor_config.heater) {
		configure_gpio(pm_meter_aux.heater_pin);
	}

	switch (oap_sensor_config.meas_strategy) {
		case 0 :
			ESP_LOGI(TAG, "measurement strategy = interval");
			params = malloc(sizeof(pm_meter_intervals_params_t));
			((pm_meter_intervals_params_t*)params)->meas_interval=oap_sensor_config.meas_interval;
			((pm_meter_intervals_params_t*)params)->meas_time=oap_sensor_config.meas_time;
			((pm_meter_intervals_params_t*)params)->warm_up_time=oap_sensor_config.warm_up_time;
			pm_meter = &pm_meter_intervals;
			break;
		case 1 :
			ESP_LOGI(TAG, "measurement strategy = continuous");
			pm_meter = &pm_meter_continuous;
			break;
		default:
			ESP_LOGE(TAG, "unknown strategy");
			return ESP_FAIL;
	}

	int pm_sensor_count = pmsx003_configure(pm_meter->input);

	pm_sensor_enable_handler_pair_t pm_sensor_handler_pair = {
		.count = pm_sensor_count
	};
	pm_sensor_handler_pair.handler[0] = pmsx003_enable_0;
	pm_sensor_handler_pair.handler[1] = pmsx003_enable_1;

	esp_err_t ret;
	for (int i = 0; i < pm_sensor_count; i++) {
		if ((ret = pmsx003_init(&pms_pair_config[i])) != ESP_OK) {
			return ret;
		}
	}
	pm_meter->start(&pm_sensor_handler_pair, params, &pm_meter_output_handler);
	return ESP_OK;
}





//--------- ENV -----------

typedef struct {
	env_data_t env_data;
	long timestamp;
} env_data_record_t;

static env_data_record_t last_env_data[2];
static bmx280_config_t bmx280_config[2];

static void env_sensor_callback(env_data_t* env_data) {
	if (env_data->sensor_idx <= 1) {
		ESP_LOGI(TAG,"env (%d): temp : %.2f C, pressure: %.2f hPa, humidity: %.2f %%", env_data->sensor_idx, env_data->temp, env_data->pressure, env_data->humidity);
		env_data_record_t* r = last_env_data + env_data->sensor_idx;
		r->timestamp = oap_epoch_sec();
		memcpy(&last_env_data->env_data, env_data, sizeof(env_data_t));
	} else {
		ESP_LOGE(TAG, "env (%d) - invalid sensor", env_data->sensor_idx);
	}
}

static void env_sensors_init() {
	memset(&last_env_data, 0, sizeof(env_data_record_t)*2);
	memset(bmx280_config, 0, sizeof(bmx280_config_t)*2);

	if (bmx280_set_hardware_config(&bmx280_config[0], 0) == ESP_OK) {
		bmx280_config[0].interval = 5000;
		bmx280_config[0].callback = &env_sensor_callback;

		if (bmx280_init(&bmx280_config[0]) != ESP_OK) {
			ESP_LOGE(TAG, "couldn't initialise bmx280 sensor %d", 0);
		}
	}

	if (bmx280_set_hardware_config(&bmx280_config[1], 1) == ESP_OK) {
		bmx280_config[1].interval = 5000;
		bmx280_config[1].callback = &env_sensor_callback;

		if (bmx280_init(&bmx280_config[1]) != ESP_OK) {
			ESP_LOGE(TAG, "couldn't initialise bmx280 sensor %d", 1);
		}
	}
}



//--------- MAIN -----------

list_t* publishers;

static void publish_all(oap_measurement_t* meas) {
	list_t* publisher = publishers->next;
	while (publisher) {
		((oap_publisher_t*)publisher->value)->publish(meas, &oap_sensor_config);
		publisher = publisher->next;
	}
}

static void publish_loop() {
	while (1) {
		long localTime = oap_epoch_sec_valid();
		long sysTime = oap_epoch_sec();
		pm_data_pair_t pm_data_pair;

		log_heap_size("publish_loop");

		if (xQueueReceive(pm_meter_result_queue, &pm_data_pair, 10000)) {
			log_task_stack(TAG);
			float aqi = fminf(pm_data_pair.pm_data[0].pm2_5 / 100.0, 1.0);
			//ESP_LOGI(TAG, "AQI=%f",aqi);
			ledc_set_color(LED_SET, aqi,(1-aqi), 0);

			oap_measurement_t meas = {
				.pm = &pm_data_pair.pm_data[0],
				.pm_aux = pm_data_pair.count == 2 ? &pm_data_pair.pm_data[1] : NULL,
				.env = sysTime - last_env_data[0].timestamp < 60 ? &last_env_data[0].env_data : NULL,
				.env_int = sysTime - last_env_data[1].timestamp < 60 ? &last_env_data[1].env_data : NULL,
				.local_time = localTime
			};

			publish_all(&meas);
		}
	}
}

static oap_sensor_config_t sensor_config_from_json(cJSON* sconfig) {
	oap_sensor_config_t sensor_config = {};
	cJSON* field;

	if ((field = cJSON_GetObjectItem(sconfig, "led"))) sensor_config.led = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "indoor"))) sensor_config.indoor = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "fan"))) sensor_config.fan = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "heater"))) sensor_config.heater = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "warmUpTime"))) sensor_config.warm_up_time = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "measTime"))) sensor_config.meas_time = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "measInterval"))) sensor_config.meas_interval = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "measStrategy"))) sensor_config.meas_strategy = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "test"))) sensor_config.test = field->valueint;
	return sensor_config;
}

void publishers_init() {
	publishers = list_createList();
	if (awsiot_publisher.configure(storage_get_config("awsiot")) == ESP_OK) {
		list_insert(publishers, &awsiot_publisher);
	}
	if (thingspeak_publisher.configure(storage_get_config("thingspeak")) == ESP_OK) {
		list_insert(publishers, &thingspeak_publisher);
	}
}

static void configure_ap_mode_btn() {
	gpio_pad_select_gpio(CONFIG_OAP_BTN_0_PIN);
	gpio_set_direction(CONFIG_OAP_BTN_0_PIN, GPIO_MODE_INPUT);
	gpio_set_pull_mode(CONFIG_OAP_BTN_0_PIN, GPIO_PULLDOWN_ONLY);
}

static int is_ap_mode_pressed() {
	return gpio_get_level(CONFIG_OAP_BTN_0_PIN);
}

void app_main() {
	delay(1000);
	ESP_LOGI(TAG,"starting app... firmware %s", oap_version_str());

	//130kb is a nice cap to test against alloc fails
	//reduce_heap_size_to(130000);
	storage_init();

	ESP_LOGD(TAG, "retrieve sensor config");
	oap_sensor_config = sensor_config_from_json(cJSON_GetObjectItem(storage_get_config("sensor"), "config"));

	//wifi/mongoose requires plenty of mem, start it here
	configure_ap_mode_btn();
	wifi_configure(is_ap_mode_pressed() ? NULL : storage_get_config("wifi"), CONFIG_OAP_CONTROL_PANEL ? cpanel_wifi_handler : NULL);
	wifi_boot();
	start_ota_task(storage_get_config("ota"));

	ledc_init();
	pm_meter_result_queue = xQueueCreate(1, sizeof(pm_data_pair_t));
	pm_meter_init();
	env_sensors_init();
	publishers_init();

	publish_loop();
}
