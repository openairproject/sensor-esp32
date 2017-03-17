/*
 * pms.c
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
#include "driver/gpio.h"
#include "driver/uart.h"
#include "soc/uart_struct.h"
#include "esp_log.h"
#include "pmsx003.h"
#include "oap_debug.h"

/*
 * Driver for Plantower PMS3003 / PMS5003 / PMS7003 dust sensors.
 * PMSx003 sensors return two values for different environment of measurement.
 */
#define OAP_PM_UART_BUF_SIZE (128)

static const char* TAG = "pmsX003";

static void pms_init_uart(pms_config_t* config) {
	//configure UART
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,//UART_HW_FLOWCTRL_CTS_RTS,
        .rx_flow_ctrl_thresh = 122,
    };
    uart_param_config(config->uart_num, &uart_config);
    uart_set_pin(config->uart_num, config->uart_txd_pin, config->uart_rxd_pin, config->uart_rts_pin, config->uart_cts_pin);
    //Install UART driver( We don't need an event queue here)

    uart_driver_install(config->uart_num, OAP_PM_UART_BUF_SIZE * 2, 0, 0, NULL,0);
}

static void pms_init_gpio(pms_config_t* config) {
	gpio_pad_select_gpio(config->set0_pin);	//CONFIG_OAP_PM_SENSOR_CONTROL_PIN
	ESP_ERROR_CHECK(gpio_set_direction(config->set0_pin, GPIO_MODE_OUTPUT));
	if (config->set1_pin) {
		gpio_pad_select_gpio(config->set1_pin);
		ESP_ERROR_CHECK(gpio_set_direction(config->set1_pin, GPIO_MODE_OUTPUT));
	}
}

static pm_data decodepm_data(uint8_t* data, uint8_t startByte) {
	pm_data pm = {
		.pm1_0 = ((data[startByte]<<8) + data[startByte+1]),
		.pm2_5 = ((data[startByte+2]<<8) + data[startByte+3]),
		.pm10 = ((data[startByte+4]<<8) + data[startByte+5])
	};
	return pm;
}

static void pms_uart_read(pms_config_t* config) {
    uint8_t* data = (uint8_t*) malloc(32);
    while(1) {
    	int len = uart_read_bytes(config->uart_num, data, 32, 100 / portTICK_RATE_MS);
        if (!config->enabled) continue;
        if (len >= 24 && data[0]==0x42 && data[1]==0x4d) {
        		log_task_stack(TAG);
        		//ESP_LOGI(TAG, "got frame of %d bytes", len);
        		pm_data pm = decodepm_data(data, config->indoor ? 4 : 10);	//atmospheric from 10th byte, standard from 4th
				if (config->callback) {
					config->callback(&pm);
				}
        } else if (len > 0) {
        	ESP_LOGW(TAG, "invalid frame of %d", len); //we often get an error after this :(
        }
    }
    vTaskDelete(NULL);
}

esp_err_t pms_enable(pms_config_t* config, int enabled) {
	ESP_LOGI(TAG,"enable(%d)",enabled);
	config->enabled = enabled;
	gpio_set_level(config->set0_pin, enabled); //low state = disabled, high state = enabled
	if (config->set1_pin) {
		gpio_set_level(config->set1_pin, enabled);
	}
	return ESP_OK; //todo
}

esp_err_t pms_init(pms_config_t* config) {
	pms_init_gpio(config);
	pms_enable(config, 0);
	pms_init_uart(config);
	//2kb leaves ~ 240 bytes free (depend on logs, printfs etc)
	xTaskCreate(pms_uart_read, "pms_uart_read", 1024*3, config, DEFAULT_TASK_PRIORITY, NULL);
	return ESP_OK;	//todo
}



