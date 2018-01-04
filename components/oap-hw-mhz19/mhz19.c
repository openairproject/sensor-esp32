/*
 * mhz19.c
 *
 *  Created on: Feb 3, 2017
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
#include "driver/gpio.h"
#include "driver/uart.h"
#include "soc/uart_struct.h"
#include "esp_log.h"
#include "mhz19.h"
#include "oap_debug.h"
#include "oap_data_env.h"

/*
 * Driver for Plantower PMS3003 / PMS5003 / PMS7003 dust sensors.
 * PMS sensors return two values for different environment of measurement.
 */
#define OAP_PM_UART_BUF_SIZE (128)

static const char* TAG = "mhz19";

esp_err_t mhz19_init_uart(mhz19_config_t* config) {
	//configure UART
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    esp_err_t ret;
    if ((ret = uart_param_config(config->uart_num, &uart_config)) != ESP_OK) {
    	return ret;
    }

    if ((ret = uart_set_pin(config->uart_num, config->uart_txd_pin, config->uart_rxd_pin, config->uart_rts_pin, config->uart_cts_pin)) != ESP_OK) {
    	return ret;
    }
    //Install UART driver( We don't need an event queue here)

    ret = uart_driver_install(config->uart_num, OAP_PM_UART_BUF_SIZE * 2, 0, 0, NULL,0);
    return ret;
}

static int mhz19_check(uint8_t *packet) {
	unsigned char checksum=0;
	for(int i=1; i<8; i++) {
		checksum += packet[i];
	}
	checksum = 0xff-checksum;
	checksum += 1;
	if(packet[8]!=checksum) {
		packet[8]=checksum;
		return 0;
	}
	return 1;
}

static esp_err_t mhz19_cmd_gc(mhz19_config_t* config) {	
	if (config->enabled) {	
		uint8_t packet[9]={0xff, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
		mhz19_check(packet);
		int len=uart_write_bytes(config->uart_num, (const char *)packet, sizeof(packet));
		if(len == 9){
			uint8_t data[32];
			len = uart_read_bytes(config->uart_num, data, sizeof(data), 100 / portTICK_RATE_MS);
			if(len == 9 && mhz19_check(data)){
				if(data[0]==0xff && data[1]==0x86) {
					int co2val=(data[2]<<8) | data[3];
					int t=data[4]-40;
					int s=data[5];
					int u=(data[6]<<8) | data[7];
					ESP_LOGD(TAG, "CO2: %d T:%d S:%d U:%d",co2val, t, s, u);
					if (config->callback && co2val > 410 && co2val < 2000) {
						env_data_t result = {
							.sensor_idx = config->sensor_idx,
							.temp = t,
							.co2 = co2val
						};
						config->callback(&result);
					}
				}
			}
		}
	}
	return ESP_OK;
}

static void mhz19_task(mhz19_config_t* config) {
    while(1) {
    	mhz19_cmd_gc(config);
	if (config->interval > 0) {
		delay(config->interval);
	} else {
		break;
	}
    }
    vTaskDelete(NULL);
}

esp_err_t mhz19_enable(mhz19_config_t* config, uint8_t enabled) {
	ESP_LOGI(TAG,"enable(%d)",enabled);
	config->enabled = enabled;
	return ESP_OK; //todo
}

esp_err_t mhz19_init(mhz19_config_t* config) {
	mhz19_enable(config, 0);
	mhz19_init_uart(config);

	char task_name[100];
	sprintf(task_name, "mhz19_sensor_%d", config->sensor_idx);

	// set ABC logic on (0xa0) / off (0x00)	
	uint8_t packet_reset[9]={0xff, 0x01, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	mhz19_check(packet_reset);
	int len=uart_write_bytes(config->uart_num, (const char *)packet_reset, sizeof(packet_reset));
	if(len!=9) {
		ESP_LOGW(TAG, "MH-Z19 reset failed");
		return ESP_FAIL;
	}
	// set range to 2000ppm
	int range = 2000;
	uint8_t packet_range[9]={ 0xff, 0x01, 0x99, range>>8, range&0xff, 0x00, 0x00, 0x00, 0x00};
	mhz19_check(packet_range);
	len=uart_write_bytes(config->uart_num, (const char *)packet_range, sizeof(packet_range));
	if(len!=9) {
		ESP_LOGW(TAG, "MH-Z19 set range failed");
		return ESP_FAIL;
	}

	//2kb leaves ~ 240 bytes free (depend on logs, printfs etc)
	xTaskCreate((TaskFunction_t)mhz19_task, task_name, 1024*3, config, DEFAULT_TASK_PRIORITY, NULL);
	return ESP_OK;	//todo
}

esp_err_t mhz19_set_hardware_config(mhz19_config_t* config, uint8_t sensor_idx) {
	config->sensor_idx = sensor_idx;
	config->uart_num = CONFIG_OAP_MH_UART_NUM;
	config->uart_txd_pin = CONFIG_OAP_MH_UART_TXD_PIN;
	config->uart_rxd_pin = CONFIG_OAP_MH_UART_RXD_PIN;
	config->uart_rts_pin = CONFIG_OAP_MH_UART_RTS_PIN;
	config->uart_cts_pin = CONFIG_OAP_MH_UART_CTS_PIN;
	return ESP_OK;
}
