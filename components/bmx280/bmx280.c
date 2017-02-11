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
#include "include/bmx280.h"

static char* TAG = "bmx280";
static QueueHandle_t samples_queue;

#define CONT_IF_I2C_OK(log, x)   do { esp_err_t rc = (x); if (rc != ESP_OK) { ESP_LOGW(TAG, "err:%s",log); if (cmd) i2c_cmd_link_delete(cmd); return rc;} } while(0);
#define CONT_IF_OK(x)   do { esp_err_t rc = (x); if (rc != ESP_OK) return rc; } while(0);

static esp_err_t read_i2c(uint8_t address, uint8_t* data, int len) {
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	//set address
	CONT_IF_I2C_OK("r1", i2c_master_start(cmd));
	CONT_IF_I2C_OK("r2", i2c_master_write_byte(cmd, (CONFIG_OAP_BMX280_ADDRESS << 1) | I2C_MASTER_WRITE, 1));
	CONT_IF_I2C_OK("r3", i2c_master_write_byte(cmd, address, 1));
	CONT_IF_I2C_OK("r4", i2c_master_stop(cmd));
	CONT_IF_I2C_OK("r5",i2c_master_cmd_begin(CONFIG_OAP_BMX280_I2C_NUM, cmd, 1000/portTICK_PERIOD_MS));
	i2c_cmd_link_delete(cmd);
	cmd = 0;

	//we need to read one byte per command (see below)
	for (int i=0;i<len;i++) {
		cmd = i2c_cmd_link_create();
		CONT_IF_I2C_OK("r6",i2c_master_start(cmd));
		CONT_IF_I2C_OK("r7",i2c_master_write_byte(cmd, (CONFIG_OAP_BMX280_ADDRESS << 1) | I2C_MASTER_READ, 1));
		CONT_IF_I2C_OK("r8",i2c_master_read(cmd,data+i,1,1)); //ACK is must!
		CONT_IF_I2C_OK("r9",i2c_master_stop(cmd));
		CONT_IF_I2C_OK("r10",i2c_master_cmd_begin(CONFIG_OAP_BMX280_I2C_NUM, cmd, 2000/portTICK_PERIOD_MS));
		i2c_cmd_link_delete(cmd);
		cmd = 0;
	}

	/* reading more than one byte per command does not work - it seems that we need ACK after each byte? (first one is read ok)
	cmd = i2c_cmd_link_create();
	CONT_IF_I2C_OK(i2c_master_start(cmd));
	CONT_IF_I2C_OK(i2c_master_write_byte(cmd, (OAP_BMX280_ADDRESS << 1) | I2C_MASTER_READ, 1));
	//reading whole packet does not work
	//CONT_IF_I2C_OK(i2c_master_read(cmd,data,len,1)); //ACK is must!
	//this also does not work
	//for (int i=0; i < len; i++) CONT_IF_I2C_OK(i2c_master_read(cmd,data+i,1,1)); //ACK is must!
	CONT_IF_I2C_OK(i2c_master_stop(cmd));
	CONT_IF_I2C_OK(i2c_master_cmd_begin(OAP_BMX280_I2C_NUM, cmd, 2000/portTICK_PERIOD_MS));
	i2c_cmd_link_delete(cmd);
	 */

	//for (int i=0; i < len; i++) ESP_LOGD(TAG, "i2c.data[%d][%d]=%d", address, i, data[i]);
	return ESP_OK;
}

static esp_err_t write_i2c_byte(uint8_t address, uint8_t value) {
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	CONT_IF_I2C_OK("w1",i2c_master_start(cmd));
	CONT_IF_I2C_OK("w2",i2c_master_write_byte(cmd, (CONFIG_OAP_BMX280_ADDRESS << 1) | I2C_MASTER_WRITE, 1));
	CONT_IF_I2C_OK("w3",i2c_master_write_byte(cmd, address, 1));
	CONT_IF_I2C_OK("w4",i2c_master_write_byte(cmd, value, 1));
	CONT_IF_I2C_OK("w5",i2c_master_stop(cmd));
	CONT_IF_I2C_OK("w6",i2c_master_cmd_begin(CONFIG_OAP_BMX280_I2C_NUM, cmd, 1000/portTICK_PERIOD_MS));
	i2c_cmd_link_delete(cmd);
	return ESP_OK;
}

static esp_err_t get_sensor_data(env_data* result)
{
	uint8_t data[24] = {0};

	CONT_IF_OK(read_i2c(0x88,data,24));

	// Convert the data
	// temp coefficents
	int dig_T1 = data[1] * 256 + data[0];
	int dig_T2 = data[3] * 256 + data[2];
	if(dig_T2 > 32767)
	{
		dig_T2 -= 65536;
	}
	int dig_T3 = data[5] * 256 + data[4];
	if(dig_T3 > 32767)
	{
		dig_T3 -= 65536;
	}
	// pressure coefficents
	int dig_P1 = data[7] * 256 + data[6];
	int dig_P2  = data[9] * 256 + data[8];
	if(dig_P2 > 32767)
	{
		dig_P2 -= 65536;
	}
	int dig_P3 = data[11]* 256 + data[10];
	if(dig_P3 > 32767)
	{
		dig_P3 -= 65536;
	}
	int dig_P4 = data[13]* 256 + data[12];
	if(dig_P4 > 32767)
	{
		dig_P4 -= 65536;
	}
	int dig_P5 = data[15]* 256 + data[14];
	if(dig_P5 > 32767)
	{
		dig_P5 -= 65536;
	}
	int dig_P6 = data[17]* 256 + data[16];
	if(dig_P6 > 32767)
	{
		dig_P6 -= 65536;
	}
	int dig_P7 = data[19]* 256 + data[18];
	if(dig_P7 > 32767)
	{
		dig_P7 -= 65536;
	}
	int dig_P8 = data[21]* 256 + data[20];
	if(dig_P8 > 32767)
	{
		dig_P8 -= 65536;
	}
	int dig_P9 = data[23]* 256 + data[22];
	if(dig_P9 > 32767)
	{
		dig_P9 -= 65536;
	}

	// Select control measurement register(0xF4)
	// Normal mode, temp and pressure over sampling rate = 1(0x27)
	CONT_IF_OK(write_i2c_byte(0xF4, 0x27));

	// Select config register(0xF5)
	// Stand_by time = 1000 ms(0xA0)

	CONT_IF_OK(write_i2c_byte(0xF5, 0xA0));
	vTaskDelay(10/portTICK_PERIOD_MS);

	// Read 8 bytes of data from register(0xF7)
	// pressure msb1, pressure msb, pressure lsb, temp msb1, temp msb, temp lsb, humidity lsb, humidity msb

	CONT_IF_OK(read_i2c(0xF7, data, 8));

	// Convert pressure and temperature data to 19-bits
	long adc_p = (((long)data[0] * 65536) + ((long)data[1] * 256) + (long)(data[2] & 0xF0)) / 16;
	long adc_t = (((long)data[3] * 65536) + ((long)data[4] * 256) + (long)(data[5] & 0xF0)) / 16;

	// Temperature offset calculations
	double var1 = (((double)adc_t) / 16384.0 - ((double)dig_T1) / 1024.0) * ((double)dig_T2);
	double var2 = ((((double)adc_t) / 131072.0 - ((double)dig_T1) / 8192.0) *(((double)adc_t)/131072.0 - ((double)dig_T1)/8192.0)) * ((double)dig_T3);
	double t_fine = (long)(var1 + var2);
	double cTemp = (var1 + var2) / 5120.0;
	//double fTemp = cTemp * 1.8 + 32;

	// Pressure offset calculations
	var1 = ((double)t_fine / 2.0) - 64000.0;
	var2 = var1 * var1 * ((double)dig_P6) / 32768.0;
	var2 = var2 + var1 * ((double)dig_P5) * 2.0;
	var2 = (var2 / 4.0) + (((double)dig_P4) * 65536.0);
	var1 = (((double) dig_P3) * var1 * var1 / 524288.0 + ((double) dig_P2) * var1) / 524288.0;
	var1 = (1.0 + var1 / 32768.0) * ((double)dig_P1);
	double p = 1048576.0 - (double)adc_p;
	p = (p - (var2 / 4096.0)) * 6250.0 / var1;
	var1 = ((double) dig_P9) * p * p / 2147483648.0;
	var2 = p * ((double) dig_P8) / 32768.0;
	double pressure = (p + (var1 + var2 + ((double)dig_P7)) / 16.0) / 100;

	result->temp = cTemp;
	result->pressure = pressure;

	return ESP_OK;
}

static void bmx280_task() {
	env_data result = {};

	while (1) {
		if (get_sensor_data(&result) == ESP_OK) {
			ESP_LOGD(TAG,"Temperature : %.2f C, Pressure: %.2f hPa", result.temp, result.pressure);
			if (!xQueueSend(samples_queue, &result, 10000/portTICK_PERIOD_MS)) {
				ESP_LOGW(TAG, "env queue overflow");
			}
		} else {
			ESP_LOGE(TAG, "error");
		}
		vTaskDelay(10000/portTICK_PERIOD_MS);
	}
}

QueueHandle_t bmx280_init() {
	samples_queue = xQueueCreate(1, sizeof(env_data));

	i2c_config_t conf;
	conf.mode = I2C_MODE_MASTER;
	conf.sda_io_num = CONFIG_OAP_BMX280_I2C_SDA_PIN;
	conf.scl_io_num = CONFIG_OAP_BMX280_I2C_SCL_PIN;
	conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
	conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
	conf.master.clk_speed = 100000;
	i2c_param_config(CONFIG_OAP_BMX280_I2C_NUM, &conf);
	i2c_driver_install(CONFIG_OAP_BMX280_I2C_NUM, I2C_MODE_MASTER, 0, 0, 0);
    xTaskCreate(bmx280_task, "bmx280_task", 1024*2, NULL, 10, NULL);
    return samples_queue;
}


