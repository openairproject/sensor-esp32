/*
 * bmp280.c
 *
 *  Created on: Feb 8, 2017
 *      Author: kris
 *
 *  based on https://github.com/LanderU/BMP280/blob/master/BMP280.c
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

#include <driver/i2c.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "sdkconfig.h"
#include "oap_common.h"
#include "oap_debug.h"
#include "include/bmx280.h"
#include "i2c_bme280.h"


static char* TAG = "bmx280";
static QueueHandle_t samples_queue;

static void bmx280_task() {
	// TODO strangely, if this is executed inside main task, LEDC fails to initialise properly PWM (and blinks in funny ways)... easy to reproduce.
	if (BME280_init(BME280_MODE_NORMAL, CONFIG_OAP_BMX280_I2C_NUM, CONFIG_OAP_BMX280_ADDRESS) == ESP_OK) {
		while(1) {
			log_task_stack(TAG);
			if (BME280_read() == ESP_OK) {
				env_data result = BME280_last_result();
				ESP_LOGD(TAG,"Temperature : %.2f C, Pressure: %.2f hPa, Humidity %.2f", result.temp, result.pressure, result.humidity);
				if (!xQueueSend(samples_queue, &result, 10000/portTICK_PERIOD_MS)) {
					ESP_LOGW(TAG, "env queue overflow");
				}
			} else {
				ESP_LOGW(TAG, "Failed to read data");
			}
			vTaskDelay(5000/portTICK_PERIOD_MS);
		}
	} else {
		ESP_LOGE(TAG, "Failed to initialise");
	}
	vTaskDelete(NULL);
}

static void i2c_setup() {
	i2c_config_t conf;
	conf.mode = I2C_MODE_MASTER;
	conf.sda_io_num = CONFIG_OAP_BMX280_I2C_SDA_PIN;
	conf.scl_io_num = CONFIG_OAP_BMX280_I2C_SCL_PIN;
	conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
	conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
	conf.master.clk_speed = 100000;
	i2c_param_config(CONFIG_OAP_BMX280_I2C_NUM, &conf);
	i2c_driver_install(CONFIG_OAP_BMX280_I2C_NUM, I2C_MODE_MASTER, 0, 0, 0);
}

QueueHandle_t bmx280_init() {
	samples_queue = xQueueCreate(1, sizeof(env_data));
	i2c_setup();
	//2kb => ~380bytes free
	xTaskCreate(bmx280_task, "bmx280_task", 1024*3, NULL, 10, NULL);
	return samples_queue;
}
