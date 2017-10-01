/*
 * led.c
 *
 *  Created on: Feb 7, 2017
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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/xtensa_api.h"
#include "freertos/queue.h"
#include "driver/ledc.h"
#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include <math.h>
#include "rgb_led.h"
#include "oap_common.h"

static const char* TAG = "rgbled";
static xQueueHandle cmd_queue;
static gpio_num_t led_gpio[] = {CONFIG_OAP_LED_R_PIN,CONFIG_OAP_LED_G_PIN,CONFIG_OAP_LED_B_PIN};

#define DEFAULT_FREQ 1500

static ledc_mode_t speed_mode = LEDC_HIGH_SPEED_MODE;

static ledc_timer_config_t ledc_timer = {
        //set timer counter bit number
        .bit_num = LEDC_TIMER_10_BIT,
        //set frequency of pwm
        .freq_hz = 5000,
        //timer mode,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        //timer index
        .timer_num = LEDC_TIMER_0
    };

static int MAX_DUTY = 0;
static rgb_color_t LED_OFF = {.v={0,0,0}};

//brightness for each of R/G/B. LED Green tends to be the brightest
//so if we use the same resistors we need to compensate for it to get proper orange.
//this probably should be configureble since it depends on particular LED and resistors.
static float brightness[] = {1.0f,0.3f,1.0f};

static int calc_duty(rgb_color_t color, uint8_t c) {
	return lroundf(MAX_DUTY * fminf(color.v[c] * brightness[c], 1.0));
}

void set_color(rgb_color_t color) {
	esp_err_t res;
	int duty;
	for (int c = 0; c < 3; c++) {
		duty = calc_duty(color,c);
		if ((res = ledc_set_duty(speed_mode, c, duty)) != ESP_OK) {
			ESP_LOGW(TAG, "ledc_set_duty(%d,%d,%d) error %d", speed_mode, c, duty, res);
		}
		if ((res = ledc_update_duty(speed_mode, c)) != ESP_OK) {
			ESP_LOGW(TAG, "ledc_update_duty(%d,%d) error %d", speed_mode, c, res);
		}
	}
}

void fade_to_color(rgb_color_t color, int time) {
	esp_err_t res;
	int duty;
	for (int c = 0; c < 3; c++) {
		duty = calc_duty(color,c);
		if ((res = ledc_set_fade_with_time(speed_mode, c, duty,time)) != ESP_OK) {
			ESP_LOGW(TAG, "ledc_set_fade_with_time(%d,%d,%d,%d) error %d", speed_mode, c, duty, time, res);
		}
		if ((res = ledc_fade_start(speed_mode, c, LEDC_FADE_NO_WAIT)) != ESP_OK) {
			ESP_LOGW(TAG, "ledc_fade_start(%d,%d) error %d", c, LEDC_FADE_NO_WAIT, res);
		}
	}
}

static void setup_ledc() {
	MAX_DUTY = pow(2,ledc_timer.bit_num)-1;
	    ledc_timer_config(&ledc_timer);

	    ledc_channel_config_t ledc_channel = {
	        .channel = -1,
	        .duty = -1,
	        .gpio_num = -1,
	        //GPIO INTR TYPE, as an example, we enable fade_end interrupt here.
	        .intr_type = LEDC_INTR_FADE_END,
	        //set LEDC mode, from ledc_mode_t
	        .speed_mode = speed_mode,
	        //set LEDC timer source, if different channel use one timer,
	        //the frequency and bit_num of these channels should be the same
	        .timer_sel = LEDC_TIMER_0
	    };

		for (int c = 0; c < 3; c++) {
			ledc_channel.channel = c; //LEDC_CHANNEL_0 to LEDC_CHANNEL_2
			ledc_channel.gpio_num = led_gpio[c];
			ledc_channel.duty = 0;
			gpio_intr_disable(led_gpio[c]);
			ledc_channel_config(&ledc_channel);
		}
	    //initialize fade service.
	    ledc_fade_func_install(0);
}

static void led_cycle() {
	rgb_color_t color = {.v={1,1,1}};
	led_cmd_t cmd = {
		.mode = LED_SET,
		.color = color
	};
	int level = 0;
	int freq;
	while (1) {
		level = !level;
		freq = cmd.freq > 0 ? cmd.freq : DEFAULT_FREQ;
		switch (cmd.mode) {
			case LED_BLINK :
				//ESP_LOGD(TAG, "->blink (%d)", level);
				set_color(level ? cmd.color : LED_OFF);
				break;
			case LED_PULSE :
				//ESP_LOGD(TAG, "->pulse (%d)", level);
				fade_to_color(level ? cmd.color : LED_OFF, freq);
			    break;
			case LED_FADE_TO:
				//ESP_LOGD(TAG, "->fade_to");
				//this is causing some errors - cannot schedule fading when other is still ongoing?
				fade_to_color(cmd.color, freq);
				break;
			default:
				//ESP_LOGD(TAG, "->set");
				freq = DEFAULT_FREQ;
				set_color(cmd.color);
				break;
		}
		//wait for another command
		if (xQueueReceive(cmd_queue, &cmd, freq / portTICK_PERIOD_MS)) {
			ESP_LOGD(TAG, "->received command");
			//memcpy(&cmd, &received, sizeof(cmd));
		}
	}
}

void led_init(int enabled, xQueueHandle _cmd_queue)
{
    if (enabled) {
    	cmd_queue = _cmd_queue;
    	setup_ledc();	//this often conflicts with other i/o operations? e.g. bme280 init.
    	xTaskCreate(led_cycle, "led_cycle", 1024*2, NULL, DEFAULT_TASK_PRIORITY+1, NULL);
    } else {
    	for (int c = 0; c < 3; c++) {
    	    if (led_gpio[c] > 0) gpio_set_pull_mode(led_gpio[c], GPIO_PULLDOWN_ONLY);
    	}
    }
}

