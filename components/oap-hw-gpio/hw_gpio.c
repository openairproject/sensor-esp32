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
#include "driver/pcnt.h"
#include "esp_log.h"
#include "hw_gpio.h"
#include "oap_debug.h"
#include "oap_data_env.h"

#define ESP_INTR_FLAG_DEFAULT 0

static const char* TAG = "hw_gpio";

typedef struct {
	uint8_t gpio_num;
	uint8_t gpio_val;
	int64_t timestamp;
	int sensor_idx;
} gpio_event_t;

static void publish(hw_gpio_config_t* config) {
	if (config->callback) {
		env_data_t result = {
					.sensor_idx = config->sensor_idx,
					.sensor_type = sensor_gpio,
					.gpio.GPIlastHigh=config->GPIlastHigh,
					.gpio.GPIlastLow=config->GPIlastLow,
					.gpio.GPICounter=config->GPICounter,
					.gpio.GPICountDelta=config->GPICounter-config->GPICountLast,
					.gpio.GPOlastOut=config->GPOlastOut,
					.gpio.GPOlastVal=config->GPOlastVal,
					.gpio.GPOtriggerLength=config->GPOtriggerLength
				};
		config->GPICountLast=config->GPICounter;
		config->callback(&result);
	}
}

esp_err_t hw_gpio_send_trigger(hw_gpio_config_t* config, int value, int delay) {
	gpio_set_level(config->output_pin, value);
	config->GPOtriggerLength=delay;
	config->GPOlastVal=value;
	config->GPOlastOut=get_time_millis();
	publish(config);
	return ESP_OK;
}

static void IRAM_ATTR hw_gpio_isr_handler(void* arg)
{
	hw_gpio_config_t* config=(hw_gpio_config_t*) arg;
	
	gpio_event_t hw_gpio_evt = {
    	.gpio_num = config->input_pin,
        .gpio_val = gpio_get_level(config->input_pin),
	.sensor_idx = config->sensor_idx
    };
    if(hw_gpio_evt.gpio_val) {
    	config->GPICounter++;
    }
    xQueueSendFromISR(config->gpio_evt_queue, &hw_gpio_evt, NULL);
}

#ifdef PCNT
typedef struct {
    int unit;  // the PCNT unit that originated an interrupt
    uint32_t status; // information on the event type that caused the interrupt
} pcnt_evt_t;

static void IRAM_ATTR pcnt_intr_handler(void *arg)
{
    hw_gpio_config_t* config=(hw_gpio_config_t*) arg;
    uint32_t intr_status = PCNT.int_st.val;
    int i;
    pcnt_evt_t evt;
    portBASE_TYPE HPTaskAwoken = pdFALSE;

    for (i = 0; i < PCNT_UNIT_MAX; i++) {
        if (intr_status & (BIT(i))) {
            evt.unit = i;
            /* Save the PCNT event type that caused an interrupt
               to pass it to the main program */
            evt.status = PCNT.status_unit[i].val;
            if(evt.status == PCNT_STATUS_H_LIM_M) {
            	pcnt_counter_clear(PCNT_UNIT_0);
	    }
            PCNT.int_clr.val = BIT(i);
            xQueueSendFromISR(config->pcnt_evt_queue, &evt, &HPTaskAwoken);
            if (HPTaskAwoken == pdTRUE) {
                portYIELD_FROM_ISR();
            }
        }
    }
}
#endif
static void hw_gpio_task(hw_gpio_config_t* config) {
    gpio_event_t hw_gpio_evt;
    while(1) {
	if(config->enabled) {
		if (xQueueReceive(config->gpio_evt_queue, &hw_gpio_evt, 10 / portTICK_PERIOD_MS)) {
//			ESP_LOGD(TAG, "%d/%d v:%d t:%d", config->sensor_idx, hw_gpio_evt.sensor_idx, hw_gpio_evt.gpio_val, hw_gpio_evt.timestamp);
			if (config->callback) {
				int64_t ms=get_time_millis();
				if(hw_gpio_evt.gpio_val) {
					config->GPIlastHigh=ms;
				} else {
					config->GPIlastLow=ms;
				}
				if((ms - config->lastPublish) > 1000) {
					config->lastPublish = ms;
					publish(config);
				}
			}
		}
#ifdef PCNT
		pcnt_evt_t evt;
	        portBASE_TYPE res = xQueueReceive(config->pcnt_evt_queue, &evt, 10 / portTICK_PERIOD_MS);
	        if (res == pdTRUE) {
			int16_t count = 0;
	        	pcnt_get_counter_value(PCNT_UNIT_0, &count);
	        	ESP_LOGD(TAG, "Event PCNT unit[%d]; cnt: %d\n", evt.unit, count);
	        	if (evt.status & PCNT_STATUS_THRES1_M) {
	        		ESP_LOGD(TAG, "THRES1 EVT\n");
			}
			if (evt.status & PCNT_STATUS_THRES0_M) {
				ESP_LOGD(TAG, "THRES0 EVT\n");
			}
			if (evt.status & PCNT_STATUS_L_LIM_M) {
				ESP_LOGD(TAG, "L_LIM EVT\n");
			}
			if (evt.status & PCNT_STATUS_H_LIM_M) {
				ESP_LOGD(TAG, "H_LIM EVT\n");
			}
			if (evt.status & PCNT_STATUS_ZERO_M) {
				ESP_LOGD(TAG, "ZERO EVT\n");
			}
 	       } else {
 	       		pcnt_get_counter_value(PCNT_UNIT_0, &count);
 	       		printf("Current counter value :%d\n", count);
		}
#endif
		if(config->GPOtriggerLength && (get_time_millis()-config->GPOlastOut) >= config->GPOtriggerLength) {
			config->GPOlastVal=!config->GPOlastVal;
			config->GPOlastOut=get_time_millis();
			gpio_set_level(config->output_pin, config->GPOlastVal);
			config->GPOtriggerLength=0;
			publish(config);
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

#ifdef PCNT
	config->pcnt_evt_queue = xQueueCreate(10, sizeof(pcnt_evt_t));
	pcnt_config_t pcnt_config = {
        	.pulse_gpio_num = config->input_pin,
        	.ctrl_gpio_num = -1,
        	.channel = PCNT_CHANNEL_0,
        	.unit = PCNT_UNIT_0,
        	.pos_mode = PCNT_COUNT_INC,
        	.neg_mode = PCNT_COUNT_INC,
        	.lctrl_mode = PCNT_MODE_KEEP,
        	.hctrl_mode = PCNT_MODE_KEEP,
        	.counter_h_lim = 32767,
        	.counter_l_lim = 0,
        };
        pcnt_unit_config(&pcnt_config);

        pcnt_set_filter_value(PCNT_UNIT_0, 100);
        pcnt_filter_enable(PCNT_UNIT_0);

//	pcnt_set_event_value(PCNT_UNIT_0, PCNT_EVT_THRES_1, 20);
//	pcnt_event_enable(PCNT_UNIT_0, PCNT_EVT_THRES_1);
	pcnt_set_event_value(PCNT_UNIT_0, PCNT_EVT_THRES_0, 10);
	pcnt_event_enable(PCNT_UNIT_0, PCNT_EVT_THRES_0);
    
	/* Enable events on zero, maximum and minimum limit values */
//    	pcnt_event_enable(PCNT_UNIT_0, PCNT_EVT_ZERO);
    	pcnt_event_enable(PCNT_UNIT_0, PCNT_EVT_H_LIM);
//    	pcnt_event_enable(PCNT_UNIT_0, PCNT_EVT_L_LIM);

    	/* Initialize PCNT's counter */
    	pcnt_counter_pause(PCNT_UNIT_0);
    	pcnt_counter_clear(PCNT_UNIT_0);

    	/* Register ISR handler and enable interrupts for PCNT unit */
    	pcnt_isr_register(pcnt_intr_handler, config, 0, NULL);
    	pcnt_intr_enable(PCNT_UNIT_0);

    	/* Everything is set up, now go to counting */
    	pcnt_counter_resume(PCNT_UNIT_0);
#endif
	gpio_pad_select_gpio(config->output_pin);
	gpio_set_direction(config->output_pin, GPIO_MODE_OUTPUT);
	gpio_set_level(config->output_pin, 0);

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
#ifdef CONFIG_OAP_GPIO_0_ENABLED
			config->output_pin = CONFIG_OAP_GPIO_OUTPUT0_PIN;
			config->input_pin = CONFIG_OAP_GPIO_INPUT0_PIN;
#else 
			return ESP_FAIL;
#endif
			break;
		case 6:
#ifdef CONFIG_OAP_GPIO_1_ENABLED
			config->output_pin = CONFIG_OAP_GPIO_OUTPUT1_PIN;
			config->input_pin = CONFIG_OAP_GPIO_INPUT1_PIN;
#else 
			return ESP_FAIL;
#endif
			break;
		case 7:
#ifdef CONFIG_OAP_GPIO_2_ENABLED
			config->output_pin = CONFIG_OAP_GPIO_OUTPUT2_PIN;
			config->input_pin = CONFIG_OAP_GPIO_INPUT2_PIN;
#else 
			return ESP_FAIL;
#endif
			break;
		case 8:
#ifdef CONFIG_OAP_GPIO_3_ENABLED
			config->output_pin = CONFIG_OAP_GPIO_OUTPUT3_PIN;
			config->input_pin = CONFIG_OAP_GPIO_INPUT3_PIN;
#else 
			return ESP_FAIL;
#endif
			break;
	}
	return ESP_OK;
}
