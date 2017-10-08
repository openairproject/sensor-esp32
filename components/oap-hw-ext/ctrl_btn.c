/*
 * btn.c
 *
 *  Created on: Feb 4, 2017
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
#include "oap_common.h"
#include "ctrl_btn.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/queue.h"

#define ESP_INTR_FLAG_DEFAULT 0
#define TAG "btn"

typedef struct {
	uint8_t gpio_num;
	uint32_t timestamp;
} gpio_event_t;

static QueueHandle_t gpio_evt_queue;
static uint32_t last_click = 0;
static btn_callback_f _callback = NULL;

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
	uint32_t t = millis();
	/*
	 * TODO is there a way to programmatically eliminate flickering?
	 */
	if (t - last_click < 80) return;
	last_click = t;
    gpio_event_t gpio_evt = {
    	.gpio_num = (uint8_t)(uint32_t)arg,
		.timestamp = t
    };
    xQueueSendFromISR(gpio_evt_queue, &gpio_evt, NULL);
}

static void gpio_watchdog_task() {
	gpio_event_t gpio_evt;
	int count = 0;
	uint32_t first_click = 0;

	while(1) {
		if (xQueueReceive(gpio_evt_queue, &gpio_evt, 1000)) {
			_callback(SINGLE_CLICK);
			//20 sec to perform the action
			if (!first_click || gpio_evt.timestamp - first_click > 20000) {
				first_click = gpio_evt.timestamp;
				count = 0;
			}
			count++;
			ESP_LOGD(TAG, "click gpio[%d] [%d in sequence]",gpio_evt.gpio_num, count);

			//due to flickering we cannot precisely count all clicks anyway
			if (count == 10) {
				_callback(MANY_CLICKS);
			}
			if (count >= 20) {
				_callback(TOO_MANY_CLICKS);
				first_click=0;
				count=0;
			}
		}
	}
}

esp_err_t btn_configure(btn_callback_f callback) {
	_callback = callback;
	gpio_evt_queue = xQueueCreate(10, sizeof(gpio_event_t));
	gpio_pad_select_gpio(CONFIG_OAP_BTN_0_PIN);
	gpio_set_direction(CONFIG_OAP_BTN_0_PIN, GPIO_MODE_INPUT);
	gpio_set_pull_mode(CONFIG_OAP_BTN_0_PIN, GPIO_PULLDOWN_ONLY);
	gpio_set_intr_type(CONFIG_OAP_BTN_0_PIN, GPIO_INTR_POSEDGE);
	gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
	gpio_isr_handler_add(CONFIG_OAP_BTN_0_PIN, gpio_isr_handler, (void*) CONFIG_OAP_BTN_0_PIN);
	xTaskCreate((TaskFunction_t)gpio_watchdog_task, "gpio_watchdog_task", 1024*2, NULL, DEFAULT_TASK_PRIORITY+2, NULL);
	return ESP_OK; //TODO handle errors
}

bool is_ap_mode_pressed() {
	return gpio_get_level(CONFIG_OAP_BTN_0_PIN);
}

