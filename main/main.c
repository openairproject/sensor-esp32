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

#include "thing_speak.h"
#include "http_get_publisher.h"
#include "meas_intervals.h"
#include "meas_continuous.h"

#include "bootwifi.h"
#include "rgb_led.h"
#include "ctrl_btn.h"
#include "bmx280.h"
#include "pmsx003.h"
#include "mhz19.h"
#include "oap_common.h"
#include "oap_storage.h"
#include "oap_debug.h"
#include "awsiot.h"
#include "ota.h"
#include "oap_data.h"
#include "ssd1366.h"
#include "hcsr04.h"
#include "hw_gpio.h"
#include "udp_server.h"
#include "web.h"

#define ESP_INTR_FLAG_DEFAULT 0

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

static void ledc_update() {
	xQueueSend(ledc_queue, &ledc_state, 100);
}

static void ledc_set_mode(led_mode_t mode) {
	ledc_state.mode = mode;
}
#ifdef CONFIG_OAP_RGB_LED
static void ledc_set_color(float r, float g, float b) {
	ledc_state.color.v[0] = r;
	ledc_state.color.v[1] = g;
	ledc_state.color.v[2] = b;
}
#endif
static void ledc_init() {
	ledc_queue = xQueueCreate(10, sizeof(led_cmd_t));
	led_init(oap_sensor_config.led, ledc_queue);
	ledc_update();
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
pm_data_pair_t pm_data_array;
extern pm_meter_t pm_meter_intervals;
extern pm_meter_t pm_meter_continuous;

#define PM_RESULT_SEND_TIMEOUT 100

static void pm_meter_result_handler(pm_data_pair_t* pm_data_pair) {
	memcpy(&pm_data_array, pm_data_pair, sizeof(pm_data_pair_t));
	if (!xQueueSend(pm_meter_result_queue, pm_data_pair, PM_RESULT_SEND_TIMEOUT)) {
		ESP_LOGW(TAG,"pm_meter_result_queue overflow");
	}
}

static void pm_meter_output_handler(pm_meter_event_t event, void* data) {
	switch (event) {
	case PM_METER_START:
		ESP_LOGI(TAG, "start measurement");
		ledc_set_mode(LED_PULSE);
		ledc_update();
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

static hcsr04_config_t hcsr04_cfg[HW_HCSR04_DEVICES_MAX];
hw_gpio_config_t hw_gpio_cfg[HW_GPIO_DEVICES_MAX];
static bmx280_config_t bmx280_config[HW_BMX280_DEVICES_MAX];
mhz19_config_t mhz19_cfg[HW_MHZ19_DEVICES_MAX];

env_data_record_t last_env_data[HW_HCSR04_DEVICES_MAX+HW_GPIO_DEVICES_MAX+HW_BMX280_DEVICES_MAX+HW_MHZ19_DEVICES_MAX];


static SemaphoreHandle_t envSemaphore = NULL;

static void env_sensor_callback(env_data_t* env_data) {
	if (env_data->sensor_idx < (sizeof(last_env_data)/sizeof(env_data_record_t))) {
	        if( xSemaphoreTakeRecursive( envSemaphore, ( TickType_t ) 1000 ) == pdTRUE ) {
	        	switch(env_data->sensor_type) {
	        		case sensor_bmx280:
					ESP_LOGI(TAG, "env (%d): temp : %.2f C, pressure: %.2f hPa, humidity: %.2f %%", env_data->sensor_idx, env_data->bmx280.temp, env_data->bmx280.pressure, env_data->bmx280.humidity);break;
	        		case sensor_mhz19:
					ESP_LOGI(TAG, "env (%d): temp : %.2f C, CO2: %d ppm", env_data->sensor_idx, env_data->mhz19.temp, env_data->mhz19.co2);break;
	        		case sensor_hcsr04:
					ESP_LOGI(TAG, "env (%d): dist: %dcm", env_data->sensor_idx, env_data->hcsr04.distance);break;
	        		case sensor_gpio:
					ESP_LOGI(TAG, "env (%d): GPIlastLow: %llu, GPIlastHigh: %llu, GPICounter: %llu, GPOlastOut: %llu, GPOlastVal: %d ", env_data->sensor_idx, env_data->gpio.GPIlastLow, env_data->gpio.GPIlastHigh, env_data->gpio.GPICounter, env_data->gpio.GPOlastOut, env_data->gpio.GPOlastVal);break;
			}
			env_data_record_t* r = last_env_data + env_data->sensor_idx;
			r->timestamp = oap_epoch_sec();
			memcpy(r, env_data, sizeof(env_data_t));
			xSemaphoreGiveRecursive(envSemaphore);
		} else {
			ESP_LOGW(TAG,"*** env waiting too long for mutex ***");
		}
	} else {
		ESP_LOGE(TAG, "env (%d) - invalid sensor", env_data->sensor_idx);
	}
}

static void env_sensors_init() {
	memset(&last_env_data, 0, sizeof(env_data_record_t)*2);
	memset(bmx280_config, 0, sizeof(bmx280_config_t)*2);
	envSemaphore = xSemaphoreCreateRecursiveMutex();
#ifdef CONFIG_OAP_BMX280_ENABLED
	if (oap_sensor_config.bmx280_enabled && bmx280_set_hardware_config(&bmx280_config[0], 0) == ESP_OK) {
		bmx280_config[0].interval = 5000;
		bmx280_config[0].callback = &env_sensor_callback;
		bmx280_config[0].altitude = oap_sensor_config.altitude;
		bmx280_config[0].tempOffset = oap_sensor_config.tempOffset;
		bmx280_config[0].humidityOffset = oap_sensor_config.humidityOffset;

		if (bmx280_init(&bmx280_config[0]) != ESP_OK) {
			ESP_LOGE(TAG, "couldn't initialise bmx280 sensor %d", 0);
		}
	}
#endif
#ifdef CONFIG_OAP_BMX280_ENABLED_AUX
	if (oap_sensor_config.bmx280_enabled && bmx280_set_hardware_config(&bmx280_config[1], 1) == ESP_OK) {
		bmx280_config[1].interval = 5000;
		bmx280_config[1].callback = &env_sensor_callback;
		bmx280_config[1].altitude = oap_sensor_config.altitude;
		bmx280_config[1].tempOffset = oap_sensor_config.tempOffset;
		bmx280_config[1].humidityOffset = oap_sensor_config.humidityOffset;

		if (bmx280_init(&bmx280_config[1]) != ESP_OK) {
			ESP_LOGE(TAG, "couldn't initialise bmx280 sensor %d", 1);
		}
	}
#endif
#ifdef CONFIG_OAP_MH_ENABLED
	if (mhz19_set_hardware_config(&mhz19_cfg[0], 2) == ESP_OK) {
		mhz19_cfg[0].interval = 5000;
		mhz19_cfg[0].callback = &env_sensor_callback;
		mhz19_init(&mhz19_cfg[0]);
		mhz19_enable(&mhz19_cfg[0], oap_sensor_config.mhz19_enabled);
	}
#endif
#ifdef CONFIG_OAP_HCSR04_0_ENABLED
	if(hcsr04_set_hardware_config(&hcsr04_cfg[0], 3) == ESP_OK) {
		hcsr04_cfg[0].interval = 1000;
		hcsr04_cfg[0].callback = &env_sensor_callback;
		hcsr04_init(&hcsr04_cfg[0]);
		hcsr04_enable(&hcsr04_cfg[0], oap_sensor_config.hcsr04_enabled);
 	}
#endif
#ifdef CONFIG_OAP_HCSR04_1_ENABLED
	if(hcsr04_set_hardware_config(&hcsr04_cfg[1], 4) == ESP_OK) {
		hcsr04_cfg[1].interval = 100;
		hcsr04_cfg[1].callback = &env_sensor_callback;
		hcsr04_init(&hcsr04_cfg[1]);
		hcsr04_enable(&hcsr04_cfg[1], oap_sensor_config.hcsr04_enabled);
 	}
#endif
#ifdef CONFIG_OAP_GPIO_0_ENABLED
	if(hw_gpio_set_hardware_config(&hw_gpio_cfg[0], 5) == ESP_OK) {
		hw_gpio_cfg[0].interval = 100;
		hw_gpio_cfg[0].callback = &env_sensor_callback;
		hw_gpio_init(&hw_gpio_cfg[0]);
		hw_gpio_enable(&hw_gpio_cfg[0], oap_sensor_config.gpio_enabled);
 	}
#endif
#ifdef CONFIG_OAP_GPIO_1_ENABLED
	if(hw_gpio_set_hardware_config(&hw_gpio_cfg[1], 6) == ESP_OK) {
		hw_gpio_cfg[1].interval = 100;
		hw_gpio_cfg[1].callback = &env_sensor_callback;
		hw_gpio_init(&hw_gpio_cfg[1]);
		hw_gpio_enable(&hw_gpio_cfg[1], oap_sensor_config.gpio_enabled);
 	}
#endif
#ifdef CONFIG_OAP_GPIO_2_ENABLED
	if(hw_gpio_set_hardware_config(&hw_gpio_cfg[2], 7) == ESP_OK) {
		hw_gpio_cfg[2].interval = 100;
		hw_gpio_cfg[2].callback = &env_sensor_callback;
		hw_gpio_init(&hw_gpio_cfg[2]);
		hw_gpio_enable(&hw_gpio_cfg[2], oap_sensor_config.gpio_enabled);
 	}
#endif
#ifdef CONFIG_OAP_GPIO_3_ENABLED
	if(hw_gpio_set_hardware_config(&hw_gpio_cfg[3], 8) == ESP_OK) {
		hw_gpio_cfg[3].interval = 100;
		hw_gpio_cfg[3].callback = &env_sensor_callback;
		hw_gpio_init(&hw_gpio_cfg[3]);
		hw_gpio_enable(&hw_gpio_cfg[3], oap_sensor_config.gpio_enabled);
 	}
#endif
#ifdef CONFIG_OAP_GPIO_UDP_ENABLED
	if(oap_sensor_config.gpio_udp_enabled) {
		xTaskCreate((TaskFunction_t)udp_server, "GPIO udp server", 1024*3, NULL, DEFAULT_TASK_PRIORITY, NULL);
	}
#endif
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
	long last_published = 0;
	
	while (1) {
		long localTime = oap_epoch_sec_valid();
		long sysTime = oap_epoch_sec();
		pm_data_pair_t pm_data_pair;
		memset(&pm_data_pair, 0, sizeof(pm_data_pair));

		log_heap_size("publish_loop");

		if (
#if defined CONFIG_OAP_PM_UART_ENABLE || defined CONFIG_OAP_PM_UART_AUX_ENABLE
			xQueueReceive(pm_meter_result_queue, &pm_data_pair, 10000) || 
#endif			
			(sysTime - last_published) > oap_sensor_config.meas_interval) {
#if !defined CONFIG_OAP_PM_UART_ENABLE && !defined CONFIG_OAP_PM_UART_AUX_ENABL
			delay(100000);
#endif	
			log_task_stack(TAG);
			last_published = sysTime;
#ifdef CONFIG_OAP_RGB_LED
			float aqi = fminf(pm_data_pair.pm_data[0].pm2_5 / 100.0, 1.0);
			//ESP_LOGI(TAG, "AQI=%f",aqi);
			ledc_set_color(aqi,(1-aqi), 0);
#endif
			ledc_set_mode(LED_SET);
			ledc_update();

			oap_measurement_t meas = {
				.pm = sysTime - pm_data_pair.timestamp < (oap_sensor_config.meas_interval*2) ? &pm_data_pair.pm_data[0]:NULL,
				.pm_aux = pm_data_pair.timestamp < (oap_sensor_config.meas_interval*2) ? (pm_data_pair.count == 2 ? &pm_data_pair.pm_data[1] : NULL):NULL,
				.env = sysTime - last_env_data[0].timestamp < 60 ? &last_env_data[0].env_data : NULL,
				.env_int = sysTime - last_env_data[1].timestamp < 60 ? &last_env_data[1].env_data : NULL,
				.co2 = sysTime - last_env_data[2].timestamp < 60 ? &last_env_data[2].env_data : NULL,
				.distance1 =  sysTime - last_env_data[3].timestamp < 60 ? &last_env_data[3].env_data : NULL,
				.distance2 =  sysTime - last_env_data[4].timestamp < 60 ? &last_env_data[4].env_data : NULL,
				.gpio1 = sysTime - last_env_data[5].timestamp < 60 ? &last_env_data[5].env_data : NULL,
				.gpio2 = sysTime - last_env_data[6].timestamp < 60 ? &last_env_data[6].env_data : NULL,
				.gpio3 = sysTime - last_env_data[7].timestamp < 60 ? &last_env_data[7].env_data : NULL,
				.gpio4 = sysTime - last_env_data[8].timestamp < 60 ? &last_env_data[8].env_data : NULL,
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
	if ((field = cJSON_GetObjectItem(sconfig, "altitude"))) sensor_config.altitude = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "tempOffset"))) sensor_config.tempOffset = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "humidityOffset"))) sensor_config.humidityOffset = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "mhz19_enabled"))) sensor_config.mhz19_enabled = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "hcsr04_enabled"))) sensor_config.hcsr04_enabled = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "ssd1306_enabled"))) sensor_config.ssd1306_enabled = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "gpio_enabled"))) sensor_config.gpio_enabled = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "pms_enabled"))) sensor_config.pms_enabled = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "bmx280_enabled"))) sensor_config.bmx280_enabled = field->valueint;
	if ((field = cJSON_GetObjectItem(sconfig, "gpio_udp_enabled"))) sensor_config.gpio_udp_enabled = field->valueint;
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
	if (http_get_publisher.configure(storage_get_config(NULL)) == ESP_OK) {
		list_insert(publishers, &http_get_publisher);
	}
}


static void btn_handler(btn_action_t action) {
	switch (action) {
		case MANY_CLICKS :
			ESP_LOGW(TAG, "about to perform factory reset!");
			ledc_set_mode(LED_BLINK);
			ledc_update();
			break;
		case TOO_MANY_CLICKS :
			reset_to_factory_partition();
			break;
		case LONG_PRESS :
			ESP_LOGW(TAG, "config reset!");
			storage_clean();
			ledc_set_mode(LED_BLINK);
			ledc_update();
			delay(1000);
			oap_reboot("reboot due to config reset");
			break;
		default:
			break;
	}
}

void display_task(void *ptr) {
	char logstr[80];
	
	ssd1306_init();
	ssd1306_display_clear();
	sprintf(logstr, "\n\nESP32-Sensor\n\nVersion %s", oap_version_str());	
	ssd1306_display_text(logstr);
	char *toggle="";
	while(1) {
		delay(5000);
		ssd1306_display_clear();
		sprintf(logstr, "%s%.2fC\n\n%.2fhPa\n\n%.2f%% / %dppm\n\nPM: %d / %d / %d",toggle,
		last_env_data[0].env_data.bmx280.temp, 
		last_env_data[0].env_data.bmx280.sealevel, 
		last_env_data[0].env_data.bmx280.humidity, 
		last_env_data[2].env_data.mhz19.co2,
		pm_data_array.pm_data[0].pm1_0,
		pm_data_array.pm_data[0].pm2_5,
		pm_data_array.pm_data[0].pm10
		);
		ssd1306_display_text(logstr);
		if(!toggle[0]) {
			toggle="\n";
		} else {
			toggle="";
		}
	}
	vTaskDelete(NULL);	
}

void app_main() {
	//silence annoying logs
	esp_log_level_set("ledc", ESP_LOG_INFO);
	esp_log_level_set("nvs", ESP_LOG_INFO);
	esp_log_level_set("tcpip_adapter", ESP_LOG_INFO);

	//a sec to start flashing
	delay(1000);
	ESP_LOGI(TAG,"starting app... firmware %s", oap_version_str());
	gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
	//130kb is a nice cap to test against alloc fails
	//reduce_heap_size_to(130000);
	storage_init();

	oap_sensor_config = sensor_config_from_json(cJSON_GetObjectItem(storage_get_config("sensor"), "config"));

	//wifi/mongoose requires plenty of mem, start it here
	btn_configure(&btn_handler);
	wifi_configure(is_ap_mode_pressed() ? NULL : storage_get_config("wifi"), CONFIG_OAP_CONTROL_PANEL ? web_wifi_handler : NULL);
	wifi_boot();
	start_ota_task(storage_get_config("ota"));

	ledc_init();
#if defined CONFIG_OAP_PM_UART_ENABLE || defined CONFIG_OAP_PM_UART_AUX_ENABLE
	pm_meter_result_queue = xQueueCreate(1, sizeof(pm_data_pair_t));
	if(oap_sensor_config.pms_enabled) {
		pm_meter_init();
	}
#endif
	env_sensors_init();
#ifdef CONFIG_OAP_SDD1306_ENABLED
	if(oap_sensor_config.ssd1306_enabled) {
		xTaskCreate((TaskFunction_t)display_task, "display task", 1024*3, NULL, DEFAULT_TASK_PRIORITY, NULL);
	}
#endif
	publishers_init();

	publish_loop();
}
