/*
The driver for the temperature, pressure, & humidity sensor BME280
Official repository: https://github.com/RyAndrew/esp8266_i2c_bme280
Adapted From: https://github.com/CHERTS/esp8266-i2c_bmp180
This driver depends on the I2C driver https://github.com/zarya/esp8266_i2c_driver/

The MIT License (MIT)

Copyright (C) 2015 Andrew Rymarczyk

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.


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

#include <math.h>
#include <driver/i2c.h>
#include <esp_log.h>

#include "i2c_bme280.h"

uint16_t calib_dig_T1;
int16_t calib_dig_T2;
int16_t calib_dig_T3;
uint16_t calib_dig_P1;
int16_t calib_dig_P2;
int16_t calib_dig_P3;
int16_t calib_dig_P4;
int16_t calib_dig_P5;
int16_t calib_dig_P6;
int16_t calib_dig_P7;
int16_t calib_dig_P8;
int16_t calib_dig_P9;
int8_t  calib_dig_H1;
int16_t calib_dig_H2;
int8_t  calib_dig_H3;
int16_t calib_dig_H4;
int16_t calib_dig_H5;
int8_t  calib_dig_H6;

uint8_t osrs_t = 1;             //Temperature oversampling x 1
uint8_t osrs_p = 1;             //Pressure oversampling x 1
uint8_t osrs_h = 1;             //Humidity oversampling x 1

uint8_t t_sb = 4;               //Tstandby, 5=1000ms, 4=500ms
uint8_t filter = 0;             //Filter off
uint8_t spi3w_en = 0;           //3-wire SPI Disable

uint8_t BME280_OperationMode = BME280_MODE_NORMAL;

#define BME280_W					0xEC
#define BME280_R					0xED
#define BME280_CHIP_ID_REG			0xD0
#define BME280_CHIP_ID				0x60

#define BME280_REG_CTRL_HUM			0xF2
#define BME280_REG_CTRL_MEAS		0xF4
#define BME280_REG_CONFIG			0xF5

#define BMP280_CHIP_ID1				(0x56)
#define BMP280_CHIP_ID2				(0x57)
#define BMP280_CHIP_ID3				(0x58)

signed long int t_fine;
signed long int temp_act;
unsigned long int press_act, hum_act;

static char* TAG = "i2c_bmx280";

static void BME280_writeConfigRegisters(void);
static void BME280_readCalibrationRegisters(void);

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

int  BME280_verifyChipId(void){
	uint8_t chipID;
	read_i2c(BME280_CHIP_ID_REG,&chipID,1);
	switch (chipID) {
		case BME280_CHIP_ID:
			ESP_LOGI(TAG,"detected BME280 (0x%X)", chipID);
			return ESP_OK;
		case BMP280_CHIP_ID1:
		case BMP280_CHIP_ID2:
		case BMP280_CHIP_ID3:
			ESP_LOGI(TAG,"detected BMP280 - no humidity data (0x%X)", chipID);
			return ESP_OK;
		default:
			ESP_LOGW(TAG,"detected unknown chip (0x%X), disable env data", chipID);
			return ESP_FAIL;
	}
}

void  BME280_writeConfigRegisters(void){

    uint8_t ctrl_meas_reg = (osrs_t << 5) | (osrs_p << 2) | BME280_OperationMode;
    uint8_t ctrl_hum_reg  = osrs_h;

    uint8_t config_reg    = (t_sb << 5) | (filter << 2) | spi3w_en;

    write_i2c_byte(BME280_REG_CTRL_HUM, ctrl_hum_reg);
    write_i2c_byte(BME280_REG_CTRL_MEAS, ctrl_meas_reg);
    write_i2c_byte(BME280_REG_CONFIG, config_reg);

}

void  BME280_readCalibrationRegisters(void){
	//temp & pressure
	// Read section 0x88
	uint8_t data[24];
	read_i2c(0x88,data,24);

	calib_dig_T1 = (data[1] << 8) | data[0];
	calib_dig_T2 = (data[3] << 8) | data[2];
	calib_dig_T3 = (data[5] << 8) | data[4];
	calib_dig_P1 = (data[7] << 8) | data[6];
	calib_dig_P2 = (data[9] << 8) | data[8];
	calib_dig_P3 = (data[11] << 8) | data[10];
	calib_dig_P4 = (data[13] << 8) | data[12];
	calib_dig_P5 = (data[15] << 8) | data[14];
	calib_dig_P6 = (data[17] << 8) | data[16];
	calib_dig_P7 = (data[19] << 8) | data[18];
	calib_dig_P8 = (data[21] << 8) | data[20];
	calib_dig_P9 = (data[23] << 8) | data[22];

	//humidity
	// Read section 0xA1
	read_i2c(0xA1,data,1);
	calib_dig_H1 = data[0];
	// Read section 0xE1
	read_i2c(0xE1,data,7);

	calib_dig_H2 = (data[1] << 8) | data[0];
	calib_dig_H3 = data[2];
	calib_dig_H4 = (data[3] << 4) | (0x0f & data[4]);
	calib_dig_H5 = (data[5] << 4) | ((data[4] >> 4) & 0x0F);
	calib_dig_H6 = data[6];
}

bool BME280_sendI2cTriggerForcedRead(){
    uint8_t ctrl_meas_reg = (osrs_t << 5) | (osrs_p << 2) | BME280_OperationMode;
    write_i2c_byte(BME280_REG_CTRL_MEAS, ctrl_meas_reg);
    vTaskDelay(10/portTICK_PERIOD_MS); // wait 10ms for worst case max sensor read time
	return 1;
}

static signed long int BME280_calibration_Temp(signed long int adc_T)
{
    signed long int var1, var2, T;
    var1 = ((((adc_T >> 3) - ((signed long int)calib_dig_T1<<1))) * ((signed long int)calib_dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((signed long int)calib_dig_T1)) * ((adc_T>>4) - ((signed long int)calib_dig_T1))) >> 12) * ((signed long int)calib_dig_T3)) >> 14;

    t_fine = var1 + var2;
    T = (t_fine * 5 + 128) >> 8;
    return T;
}

static unsigned long int BME280_calibration_Press(signed long int adc_P)
{
    signed long int var1, var2;
    unsigned long int P;
    var1 = (((signed long int)t_fine)>>1) - (signed long int)64000;
    var2 = (((var1>>2) * (var1>>2)) >> 11) * ((signed long int)calib_dig_P6);
    var2 = var2 + ((var1*((signed long int)calib_dig_P5))<<1);
    var2 = (var2>>2)+(((signed long int)calib_dig_P4)<<16);
    var1 = (((calib_dig_P3 * (((var1>>2)*(var1>>2)) >> 13)) >>3) + ((((signed long int)calib_dig_P2) * var1)>>1))>>18;
    var1 = ((((32768+var1))*((signed long int)calib_dig_P1))>>15);
    if (var1 == 0){
        return 0;
    }
    P = (((unsigned long int)(((signed long int)1048576)-adc_P)-(var2>>12)))*3125;
    if(P<0x80000000){
       P = (P << 1) / ((unsigned long int) var1);
    }else{
        P = (P / (unsigned long int)var1) * 2;
    }
    var1 = (((signed long int)calib_dig_P9) * ((signed long int)(((P>>3) * (P>>3))>>13)))>>12;
    var2 = (((signed long int)(P>>2)) * ((signed long int)calib_dig_P8))>>13;
    P = (unsigned long int)((signed long int)P + ((var1 + var2 + calib_dig_P7) >> 4));
    return P;
}

static unsigned long int BME280_calibration_Hum(signed long int adc_H)
{
    signed long int v_x1;

    v_x1 = (t_fine - ((signed long int)76800));
    v_x1 = (((((adc_H << 14) -(((signed long int)calib_dig_H4) << 20) - (((signed long int)calib_dig_H5) * v_x1)) +
              ((signed long int)16384)) >> 15) * (((((((v_x1 * ((signed long int)calib_dig_H6)) >> 10) *
              (((v_x1 * ((signed long int)calib_dig_H3)) >> 11) + ((signed long int) 32768))) >> 10) + (( signed long int)2097152)) *
              ((signed long int) calib_dig_H2) + 8192) >> 14));
   v_x1 = (v_x1 - (((((v_x1 >> 15) * (v_x1 >> 15)) >> 7) * ((signed long int)calib_dig_H1)) >> 4));
   v_x1 = (v_x1 < 0 ? 0 : v_x1);
   v_x1 = (v_x1 > 419430400 ? 419430400 : v_x1);
   return (unsigned long int)(v_x1 >> 12);
}

static env_data result = {};

bool BME280_sendI2cReadSensorData(){
    if(BME280_OperationMode == BME280_MODE_FORCED){
    	if(!BME280_sendI2cTriggerForcedRead()){
    		return 0;
    	}
    }
    uint8_t data[8];
    read_i2c(0xF7,data,8);

    unsigned long int hum_raw, temp_raw, pres_raw;

	//0xF7 - pressure
    pres_raw = (data[0] << 12) | (data[1] << 4) | (data[2] >> 4);
	//0xFA - temp
    temp_raw = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);
	//0xFD - humidity
    hum_raw  = (data[6] << 8) | data[7];

    result.temp = BME280_calibration_Temp(temp_raw) / 100.0; //Celsius
    result.pressure = BME280_calibration_Press(pres_raw) / 100.0; //hPA
    result.humidity = BME280_calibration_Hum(hum_raw) / 1024.0; // pct

    return 1;
}

void  BME280_readSensorData(){
	BME280_sendI2cReadSensorData();
}

env_data BME280_data() {
	return result;
}

bool BME280_Init(uint8_t operationMode)
{
	i2c_config_t conf;
	conf.mode = I2C_MODE_MASTER;
	conf.sda_io_num = CONFIG_OAP_BMX280_I2C_SDA_PIN;
	conf.scl_io_num = CONFIG_OAP_BMX280_I2C_SCL_PIN;
	conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
	conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
	conf.master.clk_speed = 100000;
	i2c_param_config(CONFIG_OAP_BMX280_I2C_NUM, &conf);
	i2c_driver_install(CONFIG_OAP_BMX280_I2C_NUM, I2C_MODE_MASTER, 0, 0, 0);

	if (BME280_verifyChipId() != ESP_OK) {
		return 0;
	}

	BME280_OperationMode = operationMode;

	BME280_writeConfigRegisters();

	BME280_readCalibrationRegisters();

	return 1;
}


