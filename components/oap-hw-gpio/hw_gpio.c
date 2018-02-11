/*
 * hw_gpio.c
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
#include "hw_gpio.h"
#include "oap_debug.h"
#include "oap_data_env.h"

#define ESP_INTR_FLAG_DEFAULT 0

static const char* TAG = "hw_gpio";

typedef struct {
	uint8_t gpio_num;
	uint8_t gpio_val;
	uint32_t timestamp;
	int sensor_idx;
} gpio_event_t;

static void publish(hw_gpio_config_t* config) {
	if (config->callback) {
		env_data_t result = {
					.sensor_idx = config->sensor_idx,
					.sensor_type = sensor_gpio,
					.gpio.GPIlastHigh=config->GPIlastHigh,
					.gpio.GPIlastLow=config->GPIlastLow,
					.gpio.GPOlastOut=config->GPOlastOut
				};
		config->callback(&result);
	}
}

esp_err_t hw_gpio_send_trigger(hw_gpio_config_t* config, int value, int delay) {
	gpio_set_level(config->output_pin, value);
	if(delay) {
		ets_delay_us(delay);
		gpio_set_level(config->output_pin, !value);
	}
	config->GPOlastOut=time(NULL);
	publish(config);
	return ESP_OK;
}

static void IRAM_ATTR hw_gpio_isr_handler(void* arg)
{
	hw_gpio_config_t* config=(hw_gpio_config_t*) arg;
	
	gpio_event_t hw_gpio_evt = {
    	.gpio_num = config->input_pin,
        .gpio_val = gpio_get_level(config->input_pin),
	.timestamp = system_get_time(),
	.sensor_idx = config->sensor_idx
    };
    xQueueSendFromISR(config->gpio_evt_queue, &hw_gpio_evt, NULL);
}

static void hw_gpio_task(hw_gpio_config_t* config) {
    gpio_event_t hw_gpio_evt;
//    send_trigger(config);
    while(1) {
	if(config->enabled) {
		if (xQueueReceive(config->gpio_evt_queue, &hw_gpio_evt, 	100)) {
			ESP_LOGD(TAG, "%d/%d v:%d t:%d", config->sensor_idx, hw_gpio_evt.sensor_idx, hw_gpio_evt.gpio_val, hw_gpio_evt.timestamp);
			if (config->callback) {
				if(hw_gpio_evt.gpio_val) {
					config->GPIlastHigh=time(NULL);
				} else {
					config->GPIlastLow=time(NULL);
				}
				publish(config);
			}
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

esp_err_t hw_gpio_enable(hw_gpio_config_t* config, uint8_t enabled) {
	ESP_LOGI(TAG,"enable(%d)",enabled);
	config->enabled = enabled;
	return ESP_OK; //todo
}

esp_err_t hw_gpio_init(hw_gpio_config_t* config) {
	hw_gpio_enable(config, 0);

	char task_name[100];
	sprintf(task_name, "gpio_sensor_%d", config->sensor_idx);
	config->gpio_evt_queue = xQueueCreate(10, sizeof(gpio_event_t));

	gpio_pad_select_gpio(config->input_pin);
	gpio_set_direction(config->input_pin, GPIO_MODE_INPUT);
	gpio_set_pull_mode(config->input_pin, GPIO_PULLDOWN_ONLY);
	gpio_set_intr_type(config->input_pin, GPIO_INTR_ANYEDGE);

	gpio_pad_select_gpio(config->output_pin);
	gpio_set_direction(config->output_pin, GPIO_MODE_OUTPUT);
	gpio_set_level(config->output_pin, 0);

	gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
	ESP_LOGD(TAG, "init output pin: %d input pin: %d", config->output_pin, config->input_pin);
	gpio_isr_handler_add(config->input_pin, hw_gpio_isr_handler, config);

	//2kb leaves ~ 240 bytes free (depend on logs, printfs etc)
	xTaskCreate((TaskFunction_t)hw_gpio_task, task_name, 1024*3, config, DEFAULT_TASK_PRIORITY, NULL);
	return ESP_OK;	//todo
}

esp_err_t hw_gpio_set_hardware_config(hw_gpio_config_t* config, uint8_t sensor_idx) {
	config->sensor_idx = sensor_idx;
	switch(sensor_idx) {
		case 5:
#if CONFIG_OAP_GPIO_0_ENABLED
			config->output_pin = CONFIG_OAP_GPIO_OUTPUT0_PIN;
			config->input_pin = CONFIG_OAP_GPIO_INPUT0_PIN;
#endif
			break;
		case 6:
#if CONFIG_OAP_GPIO_1_ENABLED
			config->output_pin = CONFIG_OAP_GPIO_OUTPUT1_PIN;
			config->input_pin = CONFIG_OAP_GPIO_OUTPUT1_PIN;
#endif
			break;
	}
	return ESP_OK;
}
