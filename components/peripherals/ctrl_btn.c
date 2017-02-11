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
//#include "oap_config.h"
#include "ctrl_btn.h"
#include "esp_log.h"

#define ESP_INTR_FLAG_DEFAULT 0

static QueueHandle_t gpio_evt_queue = NULL;
static QueueHandle_t btn_events = NULL;


static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static long int now() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static uint8_t gpio_to_index[40] = {-1};

static void btn_task()
{
    uint32_t io_num;
    uint8_t level[40] = {0};
    long int time = now();
    long int _time;


    for(;;) {
        if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {

        	//debounce
        	_time = now();
        	if (_time - time > 50 || io_num >= 40) {
        		btn_event event = {
        			.index = gpio_to_index[io_num],
        			.gpio = io_num,
					.level= gpio_get_level(io_num)
        		};

        		if (level[event.gpio] == event.level) continue;

        		time = _time;
        		level[event.gpio] = event.level;
        		xQueueSend(btn_events, &event, 500);
        	}
        }
    }
}


QueueHandle_t btn_init(gpio_num_t *btn_gpio, uint8_t num_of_btns)
{
	btn_events = xQueueCreate(1, sizeof(btn_event));
    gpio_config_t io_conf;

    //interrupt of ...
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    //bit mask of the pins,
    io_conf.pin_bit_mask = 0;
    //set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    //set pull-up mode
    io_conf.pull_up_en = 0;
    io_conf.pull_down_en = 1;

    for (int n = 0; n < num_of_btns; n++) {
    	io_conf.pin_bit_mask |=  (uint64_t)(((uint64_t)1)<<((uint64_t)(btn_gpio[n])));
    }

    gpio_config(&io_conf);

    //create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    //start gpio task
    xTaskCreate(btn_task, "btn_task", 2048, NULL, 10, NULL);

    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    //hook isr handler for specific gpio pin
    for (int n = 0; n < num_of_btns; n++) {
    	gpio_to_index[btn_gpio[n]] = n;
    	gpio_isr_handler_add(btn_gpio[n], gpio_isr_handler, (void*) btn_gpio[n]);
    }

    return btn_events;
}


