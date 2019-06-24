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
#include <math.h>

#include "sdkconfig.h"
#include "oap_common.h"
#include "oap_debug.h"
#include "bmx280.h"
#include "i2c_bme280.h"

static char* TAG = "bmx280";


static double getHumidityForTemp(double srcHumidity, double temp, double temp2) {
	double a = 7.5, b = 237.3;
	double mw = 18.016;
	double Rstar = 8314.3;
	
	if(temp < 0) {
		a=7.6;
		b=240.7;
	}
	double sdd = 6.1078 * pow(10, (a*temp)/(b+temp));
	double dd = srcHumidity/100 * sdd;
	double af = pow(10,5) * mw/Rstar * dd/ ( temp + 273.15);
	return  100 * (temp2 + 273.15) * Rstar * af/(pow(10,5) * mw * 6.1078 * pow(10, (a*temp2)/(b+temp2)));
}

static float getPressureAtSeaLevel(float altitude, float pressure) {
    float gradient = 0.0065;
    float tempAtSea = 15.0;
    tempAtSea += 273.15;  // °C to K
    return pressure / pow((1 - gradient * altitude / tempAtSea), (0.03416 / gradient));
}

esp_err_t bmx280_measurement_loop(bmx280_config_t* bmx280_config) {
	i2c_comm_t i2c_comm = {
		.i2c_num = bmx280_config->i2c_num,
		.device_addr = bmx280_config->device_addr
	};

	bme280_sensor_t bmx280_sensor = {
		.operation_mode = BME280_MODE_NORMAL,
		.i2c_comm = i2c_comm
	};

	env_data_t result = {
		.sensor_idx = bmx280_config->sensor_idx,
		.sensor_type = sensor_bmx280
	};

	// TODO strangely, if this is executed inside main task, LEDC fails to initialise properly PWM (and blinks in funny ways)... easy to reproduce.
	esp_err_t ret;
	if ((ret = BME280_init(&bmx280_sensor)) == ESP_OK) {
		while(1) {
			log_task_stack(TAG);
			if ((ret = BME280_read(&bmx280_sensor, &result)) == ESP_OK) {
				result.bmx280.sealevel = getPressureAtSeaLevel(bmx280_config->altitude, result.bmx280.pressure);
				ESP_LOGD(TAG,"sensor (%d) => Temperature : %.2f C, Pressure: %.2f hPa, Pressure: %.2f hPa @ %dm,Humidity %.2f", result.sensor_idx, result.bmx280.temp, result.bmx280.pressure, result.bmx280.sealevel, bmx280_config->altitude, result.bmx280.humidity);
				result.bmx280.humidity = getHumidityForTemp(result.bmx280.humidity, result.bmx280.temp, result.bmx280.temp + bmx280_config->tempOffset);
				result.bmx280.temp += bmx280_config->tempOffset;
				result.bmx280.humidity += bmx280_config->humidityOffset;
				if (bmx280_config->callback) {
					bmx280_config->callback(&result);
				}
			} else {
				ESP_LOGW(TAG, "Failed to read data");
			}
			if (bmx280_config->interval > 0) {
				delay(bmx280_config->interval);
			} else {
				break;
			}
		}
	} else {
		ESP_LOGE(TAG, "Failed to initialise");
	}
	return ret;
}

static void bmx280_task(bmx280_config_t* bmx280_config) {
	bmx280_measurement_loop(bmx280_config);
	vTaskDelete(NULL);
}

static uint8_t i2c_drivers[3] = {0};

esp_err_t bmx280_i2c_setup(bmx280_config_t* config) {
	if (config->i2c_num > 2) {
		ESP_LOGE(TAG, "invalid I2C BUS NUMBER (%d)", config->i2c_num);
		return ESP_FAIL;
	}
	if (i2c_drivers[config->i2c_num]) return ESP_OK; //already installed

	i2c_config_t i2c_conf;
	i2c_conf.mode = I2C_MODE_MASTER;
	i2c_conf.sda_io_num = config->sda_pin;//CONFIG_OAP_BMX280_I2C_SDA_PIN;
	i2c_conf.scl_io_num = config->scl_pin;//CONFIG_OAP_BMX280_I2C_SCL_PIN;
	i2c_conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
	i2c_conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
	i2c_conf.master.clk_speed = 1000000;

	esp_err_t res;
	if ((res = i2c_param_config(config->i2c_num, &i2c_conf)) != ESP_OK) return res;

	ESP_LOGD(TAG, "install I2C driver (bus %d, sda %d, scl %d)", config->i2c_num, i2c_conf.sda_io_num, i2c_conf.scl_io_num);
	res = i2c_driver_install(config->i2c_num, I2C_MODE_MASTER, 0, 0, 0);
	if (res == ESP_OK) {
		i2c_drivers[config->i2c_num] = 1;
	}

	return res;
}

esp_err_t bmx280_init(bmx280_config_t* bmx280_config) {
	esp_err_t res;
	if ((res = bmx280_i2c_setup(bmx280_config)) == ESP_OK) {
		//2kb => ~380bytes free
		xTaskCreate((TaskFunction_t)bmx280_task, "bmx280_task", 1024*3, bmx280_config, DEFAULT_TASK_PRIORITY, NULL);
	}
	return res;
}

esp_err_t bmx280_set_hardware_config(bmx280_config_t* bmx280_config, uint8_t sensor_idx) {
	switch (sensor_idx) {
	case 0:
#ifdef CONFIG_OAP_BMX280_ENABLED
		bmx280_config->sensor_idx = 0;
		bmx280_config->i2c_num = CONFIG_OAP_BMX280_I2C_NUM;
		bmx280_config->device_addr = CONFIG_OAP_BMX280_ADDRESS;
		bmx280_config->sda_pin = CONFIG_OAP_BMX280_I2C_SDA_PIN;
		bmx280_config->scl_pin = CONFIG_OAP_BMX280_I2C_SCL_PIN;
#else
		return ESP_FAIL;
#endif
		return ESP_OK;
	case 1:
#ifdef CONFIG_OAP_BMX280_ENABLED_AUX 
		bmx280_config->sensor_idx = 1;
		bmx280_config->i2c_num = CONFIG_OAP_BMX280_I2C_NUM_AUX;
		bmx280_config->device_addr = CONFIG_OAP_BMX280_ADDRESS_AUX;
		bmx280_config->sda_pin = CONFIG_OAP_BMX280_I2C_SDA_PIN_AUX;
		bmx280_config->scl_pin = CONFIG_OAP_BMX280_I2C_SCL_PIN_AUX;
#else
		return ESP_FAIL;
#endif
		return ESP_OK;
	default:
		return ESP_FAIL;
	}
}
