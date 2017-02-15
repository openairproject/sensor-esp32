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
//#include "oap_config.h"

static xQueueHandle cmd_queue;
static gpio_num_t led_gpio[] = {CONFIG_OAP_LED_R_PIN,CONFIG_OAP_LED_G_PIN,CONFIG_OAP_LED_B_PIN};

#define BRIGHTNESS 1.0
#define DEFAULT_FREQ 1500;

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
static rgb LED_OFF = {.v={0,0,0}};

static int calc_duty(rgb color, uint8_t c) {
	return lroundf(MAX_DUTY * fminf(color.v[c] * BRIGHTNESS, 1.0));
}

void set_color(rgb color) {
	for (int c = 0; c < 3; c++) {
		ledc_set_duty(speed_mode, c, calc_duty(color,c));
		ledc_update_duty(speed_mode, c);
	}
}

void fade_to_color(rgb color, int time) {
	for (int c = 0; c < 3; c++) {
		ledc_set_fade_with_time(speed_mode, c, calc_duty(color,c),time);
		ledc_fade_start(c, LEDC_FADE_NO_WAIT);
	}
}

static void led_cycle() {
	rgb color = {.v={1,1,1}};
	led_cmd cmd = {
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
				set_color(level ? cmd.color : LED_OFF);
				break;
			case LED_PULSE :
				fade_to_color(level ? cmd.color : LED_OFF, freq);
			    break;
			case LED_FADE_TO:
				//this is causing some errors - cannot schedule fading when other is still ongoing?
				fade_to_color(cmd.color, freq);
				break;
			default:
				freq = DEFAULT_FREQ;
				set_color(cmd.color);
				break;
		}
		//wait for another command
		xQueueReceive(cmd_queue, &cmd, freq / portTICK_PERIOD_MS);
	}
}

void led_init(int enabled, xQueueHandle _cmd_queue)
{
	cmd_queue = _cmd_queue;
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

    if (enabled) {
        //initialize fade service.
        ledc_fade_func_install(0);
    	xTaskCreate(led_cycle, "led_cycle", 1024*2, NULL, 10, NULL);
    }
}

