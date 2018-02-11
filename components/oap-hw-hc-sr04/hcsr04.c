/*
 * hcsr04.c
 *
 *  Created on: Jan 4, 2018
 *      Author: Deti
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
#include "driver/gpio.h"
#include "esp_log.h"
#include "hcsr04.h"
#include "oap_debug.h"
#include "oap_data_env.h"

#define ESP_INTR_FLAG_DEFAULT 0

static const char* TAG = "hcsr04";

typedef struct {
	uint8_t gpio_num;
	uint8_t gpio_val;
	uint32_t timestamp;
	int sensor_idx;
} hcsr04_event_t;

static double calc_dist(int time_diff) {
    return 100*((time_diff/1000000.0)*340.29)/2;
}

static void send_trigger(hcsr04_config_t* config) {
	gpio_set_level(config->trigger_pin, 1);
	ets_delay_us(10);
	gpio_set_level(config->trigger_pin, 0);
	config->state=WAITFORECHO;
}

static void IRAM_ATTR hcsr04_isr_handler(void* arg)
{
	hcsr04_config_t* config=(hcsr04_config_t*) arg;
	
	hcsr04_event_t hcsr04_evt = {
    	.gpio_num = config->echo_pin,
        .gpio_val = gpio_get_level(config->echo_pin),
	.timestamp = system_get_time(),
	.sensor_idx = config->sensor_idx
    };
    xQueueSendFromISR(config->hcsr04_evt_queue, &hcsr04_evt, NULL);
}

static void hcsr04_task(hcsr04_config_t* config) {
    hcsr04_event_t gpio_evt;
    uint32_t startpulse=0;
    uint32_t endpulse=0;
//    send_trigger(config);
    while(1) {
	if(config->enabled) {
		if (xQueueReceive(config->hcsr04_evt_queue, &gpio_evt, 	100)) {
		ESP_LOGD(TAG, "%d/%d s:%d v:%d t:%d", config->sensor_idx, gpio_evt.sensor_idx, config->state, gpio_evt.gpio_val, gpio_evt.timestamp);
			switch(config->state) {
				case WAITFORECHO:
					if(gpio_evt.gpio_val) {
						startpulse=gpio_evt.timestamp;
						config->state = RECEIVE;
						ESP_LOGD(TAG, "sensor: %d state: %d start pulse", config->sensor_idx, config->state);
					} else {
						ESP_LOGD(TAG, "sensor: %d state: %d invalid val: %d", config->sensor_idx, config->state, gpio_evt.gpio_val);
					}
					break;
				case RECEIVE:
					if(!gpio_evt.gpio_val) {
						endpulse=gpio_evt.timestamp;
						double distance = calc_dist(endpulse-startpulse);
						ESP_LOGD(TAG, "sensor: %d difference: %d distance: %.2f", config->sensor_idx, endpulse-startpulse, distance);
						if (distance <= 400 && config->callback) {
							env_data_t result = {
								.sensor_idx = config->sensor_idx,
								.sensor_type = sensor_hcsr04,
								.hcsr04.distance = sma_generator(&config->sma, distance)
							};
							config->callback(&result);
						}
						config->state = IDLE;
					} else {
						ESP_LOGD(TAG, "sensor: %d state: %d invalid val: %d", config->sensor_idx, config->state, gpio_evt.gpio_val);
					}
					break;
				case IDLE:
					break;
				
			}
		} else {
			ESP_LOGD(TAG, "sensor: %d ping", config->sensor_idx);
			send_trigger(config);
		}
	}

	if (config->interval > 0) {
		delay(config->interval);
	} else {
		break;
	}
    }
    vTaskDelete(NULL);
}

esp_err_t hcsr04_enable(hcsr04_config_t* config, uint8_t enabled) {
	ESP_LOGI(TAG,"enable(%d)",enabled);
	config->enabled = enabled;
	return ESP_OK; //todo
}

esp_err_t hcsr04_init(hcsr04_config_t* config) {
	hcsr04_enable(config, 0);

	char task_name[100];
	sprintf(task_name, "hcsr04_sensor_%d", config->sensor_idx);
	config->hcsr04_evt_queue = xQueueCreate(10, sizeof(hcsr04_event_t));

	gpio_pad_select_gpio(config->echo_pin);
	gpio_set_direction(config->echo_pin, GPIO_MODE_INPUT);
	gpio_set_pull_mode(config->echo_pin, GPIO_PULLDOWN_ONLY);
	gpio_set_intr_type(config->echo_pin, GPIO_INTR_ANYEDGE);

	gpio_pad_select_gpio(config->trigger_pin);
	gpio_set_direction(config->trigger_pin, GPIO_MODE_OUTPUT);
	gpio_set_level(config->trigger_pin, 0);

	gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
	config->state = IDLE;
	ESP_LOGD(TAG, "init Trigger: %d Echo: %d", config->trigger_pin, config->echo_pin);
	gpio_isr_handler_add(config->echo_pin, hcsr04_isr_handler, config);

	//2kb leaves ~ 240 bytes free (depend on logs, printfs etc)
	xTaskCreate((TaskFunction_t)hcsr04_task, task_name, 1024*3, config, DEFAULT_TASK_PRIORITY, NULL);
	return ESP_OK;	//todo
}

#define SMA_SIZE 5
esp_err_t hcsr04_set_hardware_config(hcsr04_config_t* config, uint8_t sensor_idx) {
	config->sensor_idx = sensor_idx;
	switch(sensor_idx) {
		case 3:
#if CONFIG_OAP_HCSR04_0_ENABLED
			config->trigger_pin = CONFIG_OAP_HCSR04_TRIGGER0_PIN;
			config->echo_pin = CONFIG_OAP_HCSR04_ECHO0_PIN;
#endif
			break;
		case 4:
#if CONFIG_OAP_HCSR04_1_ENABLED
			config->trigger_pin = CONFIG_OAP_HCSR04_TRIGGER1_PIN;
			config->echo_pin = CONFIG_OAP_HCSR04_ECHO1_PIN;
#endif
			break;
	}
	memset(&config->sma, 0, sizeof(sma_data_t));
	config->sma.data = (double *)malloc(SMA_SIZE*sizeof(double));
	memset((void*)config->sma.data, 0, SMA_SIZE*sizeof(double));
	config->sma.size=(size_t)SMA_SIZE;
	return ESP_OK;
}
