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

static void bmx280_task(bmx280_config_t* bmx280_config) {

	i2c_comm_t i2c_comm = {
		.i2c_num = bmx280_config->i2c_num,
		.device_addr = bmx280_config->device_addr
	};

	bme280_sensor_t bmx280_sensor = {
		.operation_mode = BME280_MODE_NORMAL,
		.i2c_comm = i2c_comm
	};

	env_data result = {};

	// TODO strangely, if this is executed inside main task, LEDC fails to initialise properly PWM (and blinks in funny ways)... easy to reproduce.
	if (BME280_init(&bmx280_sensor) == ESP_OK) {
		while(1) {
			log_task_stack(TAG);
			if (BME280_read(&bmx280_sensor, &result) == ESP_OK) {
				ESP_LOGD(TAG,"Temperature : %.2f C, Pressure: %.2f hPa, Humidity %.2f", result.temp, result.pressure, result.humidity);
				if (!xQueueSend(samples_queue, &result, 10000/portTICK_PERIOD_MS)) {
					ESP_LOGW(TAG, "env queue overflow");
				}
			} else {
				ESP_LOGW(TAG, "Failed to read data");
			}
			delay(5000);
		}
	} else {
		ESP_LOGE(TAG, "Failed to initialise");
	}
	vTaskDelete(NULL);
}

static void i2c_setup(bmx280_config_t* config) {
	i2c_config_t i2c_conf;
	i2c_conf.mode = I2C_MODE_MASTER;
	i2c_conf.sda_io_num = config->sda_pin;//CONFIG_OAP_BMX280_I2C_SDA_PIN;
	i2c_conf.scl_io_num = config->scl_pin;//CONFIG_OAP_BMX280_I2C_SCL_PIN;
	i2c_conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
	i2c_conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
	i2c_conf.master.clk_speed = 100000;
	i2c_param_config(config->i2c_num, &i2c_conf);
	i2c_driver_install(config->i2c_num, I2C_MODE_MASTER, 0, 0, 0);
}

QueueHandle_t bmx280_init(bmx280_config_t* bmx280_config) {
	samples_queue = xQueueCreate(1, sizeof(env_data));
	i2c_setup(bmx280_config);
	//2kb => ~380bytes free
	xTaskCreate(bmx280_task, "bmx280_task", 1024*3, bmx280_config, DEFAULT_TASK_PRIORITY, NULL);
	return samples_queue;
}
