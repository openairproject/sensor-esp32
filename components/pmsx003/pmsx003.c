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
#include "oap_common.h"

/*
 * Driver for Plantower PMS3003 and PMS5003 dust sensors (not tested with PMS7003 yet).
 * PMSx003 sensors return two values for different environment of measurement.
 */
#define OAP_PM_UART_BUF_SIZE (128)

static const char* TAG = "pmsX003";

static int enabled = 0;
static int indoor = 0;
static QueueHandle_t samples;

static void pms_init_uart(void) {
	//configure UART
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,//UART_HW_FLOWCTRL_CTS_RTS,
        .rx_flow_ctrl_thresh = 122,
    };
    uart_param_config(CONFIG_OAP_PM_UART_NUM, &uart_config);
    uart_set_pin(CONFIG_OAP_PM_UART_NUM, CONFIG_OAP_PM_UART_TXD_PIN, CONFIG_OAP_PM_UART_RXD_PIN, CONFIG_OAP_PM_UART_RTS_PIN, CONFIG_OAP_PM_UART_CTS_PIN);
    //Install UART driver( We don't need an event queue here)

    uart_driver_install(CONFIG_OAP_PM_UART_NUM, OAP_PM_UART_BUF_SIZE * 2, 0, 0, NULL,0);
}

static void pms_init_gpio(void) {
	gpio_pad_select_gpio(CONFIG_OAP_PM_SENSOR_CONTROL_PIN);
	ESP_ERROR_CHECK(gpio_set_direction(CONFIG_OAP_PM_SENSOR_CONTROL_PIN, GPIO_MODE_OUTPUT));
}

static pm_data decodepm_data(uint8_t* data, uint8_t startByte) {
	pm_data pm = {
		.pm1_0 = ((data[startByte]<<8) + data[startByte+1]),
		.pm2_5 = ((data[startByte+2]<<8) + data[startByte+3]),
		.pm10 = ((data[startByte+4]<<8) + data[startByte+5])
	};
	return pm;
}

static void pms_uart_read()
{
    uint8_t* data = (uint8_t*) malloc(32);
    while(1) {
        int len = uart_read_bytes(CONFIG_OAP_PM_UART_NUM, data, 32, 100 / portTICK_RATE_MS);
        if (!enabled) continue;
        if (len >= 24 && data[0]==0x42 && data[1]==0x4d) {
        		//ESP_LOGI(TAG, "got frame of %d bytes", len);
        		pm_data pm = decodepm_data(data, indoor ? 4 : 10);	//atmospheric from 10th byte, standard from 4th
				if (!xQueueSend(samples, &pm, 100)) {
					ESP_LOGW(TAG, "sample queue overflow");
				}
        } else if (len > 0) {
        	ESP_LOGW(TAG, "invalid frame of %d", len);
        }
    }
}

void pms_enable(int _enabled) {
	enabled = _enabled;
	ESP_LOGI(TAG,"enable(%d)",enabled);
	gpio_set_level(CONFIG_OAP_PM_SENSOR_CONTROL_PIN, enabled); //low state = disabled, high state = enabled
}

QueueHandle_t pms_init(int _indoor) {
	indoor = _indoor;
	samples = xQueueCreate(1, sizeof(pm_data));
	pms_init_gpio();
	pms_enable(0);
	pms_init_uart();

	xTaskCreate(pms_uart_read, "pms_uart_read", 1024, NULL, 10, NULL);
	return samples;
}



