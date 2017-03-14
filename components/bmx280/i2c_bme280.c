/*
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

/*
 * this driver works fine for both BMP280 and BME280.
 * BMP280 reports humidity = 0%.
 */

typedef int64_t BME280_S64_t;
typedef uint32_t BME280_U32_t;
typedef int32_t BME280_S32_t;

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

static uint16_t dig_T1;
static int16_t dig_T2;
static int16_t dig_T3;
static uint16_t dig_P1;
static int16_t dig_P2;
static int16_t dig_P3;
static int16_t dig_P4;
static int16_t dig_P5;
static int16_t dig_P6;
static int16_t dig_P7;
static int16_t dig_P8;
static int16_t dig_P9;
static int8_t  dig_H1;
static int16_t dig_H2;
static int8_t  dig_H3;
static int16_t dig_H4;
static int16_t dig_H5;
static int8_t  dig_H6;

static uint8_t osrs_t = 1;             //Temperature oversampling x 1
static uint8_t osrs_p = 1;             //Pressure oversampling x 1
static uint8_t osrs_h = 1;             //Humidity oversampling x 1

static uint8_t t_sb = 4;               //Tstandby, 5=1000ms, 4=500ms
static uint8_t filter = 0;             //Filter off
static uint8_t spi3w_en = 0;           //3-wire SPI Disable

static uint8_t operation_mode;
static uint8_t i2c_num;
static uint8_t device_addr;

BME280_S32_t t_fine;
BME280_S32_t temp_act;
BME280_U32_t press_act, hum_act;

static char* TAG = "i2c_bmx280";

static env_data result = {};

#define CONT_IF_I2C_OK(log, x)   do { esp_err_t rc = (x); if (rc != ESP_OK) { ESP_LOGW(TAG, "err: %d (%s)",rc,log); if (cmd) i2c_cmd_link_delete(cmd); return rc;} } while(0);
#define CONT_IF_OK(x)   do { esp_err_t rc = (x); if (rc != ESP_OK) return rc; } while(0);

static esp_err_t read_i2c(uint8_t reg, uint8_t* data, int len) {
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	//set address
	CONT_IF_I2C_OK("r1", i2c_master_start(cmd));
	CONT_IF_I2C_OK("r2", i2c_master_write_byte(cmd, (device_addr << 1) | I2C_MASTER_WRITE, 1));
	CONT_IF_I2C_OK("r3", i2c_master_write_byte(cmd, reg, 1));
	CONT_IF_I2C_OK("r4", i2c_master_stop(cmd));
	CONT_IF_I2C_OK("r5",i2c_master_cmd_begin(i2c_num, cmd, 1000/portTICK_PERIOD_MS)); //often ESP_FAIL (no ack received)
	i2c_cmd_link_delete(cmd);
	cmd = 0;

	//we need to read one byte per command (see below)
	for (int i=0;i<len;i++) {
		cmd = i2c_cmd_link_create();
		CONT_IF_I2C_OK("r6",i2c_master_start(cmd));
		CONT_IF_I2C_OK("r7",i2c_master_write_byte(cmd, (device_addr << 1) | I2C_MASTER_READ, 1));
		CONT_IF_I2C_OK("r8",i2c_master_read(cmd,data+i,1,1)); //ACK is must!
		CONT_IF_I2C_OK("r9",i2c_master_stop(cmd));
		CONT_IF_I2C_OK("r10",i2c_master_cmd_begin(i2c_num, cmd, 2000/portTICK_PERIOD_MS));
		i2c_cmd_link_delete(cmd);
		cmd = 0;
	}
	return ESP_OK;
}

static esp_err_t write_i2c_byte(uint8_t reg, uint8_t value) {
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	CONT_IF_I2C_OK("w1",i2c_master_start(cmd));
	CONT_IF_I2C_OK("w2",i2c_master_write_byte(cmd, (device_addr << 1) | I2C_MASTER_WRITE, 1));
	CONT_IF_I2C_OK("w3",i2c_master_write_byte(cmd, reg, 1));
	CONT_IF_I2C_OK("w4",i2c_master_write_byte(cmd, value, 1));
	CONT_IF_I2C_OK("w5",i2c_master_stop(cmd));
	CONT_IF_I2C_OK("w6",i2c_master_cmd_begin(i2c_num, cmd, 1000/portTICK_PERIOD_MS));
	i2c_cmd_link_delete(cmd);
	return ESP_OK;
}

static esp_err_t BME280_writeConfigRegisters() {
    uint8_t ctrl_meas_reg = (osrs_t << 5) | (osrs_p << 2) | operation_mode;
    uint8_t ctrl_hum_reg  = osrs_h;

    uint8_t config_reg    = (t_sb << 5) | (filter << 2) | spi3w_en; //500ms interval

    return write_i2c_byte(BME280_REG_CTRL_HUM, ctrl_hum_reg) == ESP_OK &&
    	   write_i2c_byte(BME280_REG_CTRL_MEAS, ctrl_meas_reg) == ESP_OK &&
		   write_i2c_byte(BME280_REG_CONFIG, config_reg) == ESP_OK ? ESP_OK : ESP_FAIL;
}

static esp_err_t trigger_force_read(){
    uint8_t ctrl_meas_reg = (osrs_t << 5) | (osrs_p << 2) | operation_mode;
    if (write_i2c_byte(BME280_REG_CTRL_MEAS, ctrl_meas_reg) != ESP_OK) return ESP_FAIL;
    vTaskDelay(10/portTICK_PERIOD_MS); // wait 10ms for worst case max sensor read time
	return ESP_OK;
}

static esp_err_t BME280_readCalibrationRegisters(void){
	//temp & pressure
	// Read section 0x88
	uint8_t data[24];
	if (read_i2c(0x88,data,24) != ESP_OK) return ESP_FAIL;

	dig_T1 = (data[1] << 8) | data[0];
	dig_T2 = (data[3] << 8) | data[2];
	dig_T3 = (data[5] << 8) | data[4];
	dig_P1 = (data[7] << 8) | data[6];
	dig_P2 = (data[9] << 8) | data[8];
	dig_P3 = (data[11] << 8) | data[10];
	dig_P4 = (data[13] << 8) | data[12];
	dig_P5 = (data[15] << 8) | data[14];
	dig_P6 = (data[17] << 8) | data[16];
	dig_P7 = (data[19] << 8) | data[18];
	dig_P8 = (data[21] << 8) | data[20];
	dig_P9 = (data[23] << 8) | data[22];

	//humidity
	// Read section 0xA1
	if (read_i2c(0xA1,data,1) != ESP_OK) return ESP_FAIL;
	dig_H1 = data[0];
	// Read section 0xE1
	if (read_i2c(0xE1,data,7) != ESP_OK) return ESP_FAIL;

	dig_H2 = (data[1] << 8) | data[0];
	dig_H3 = data[2];
	dig_H4 = (data[3] << 4) | (0x0f & data[4]);
	dig_H5 = (data[5] << 4) | ((data[4] >> 4) & 0x0F);
	dig_H6 = data[6];
	return ESP_OK;
}

/**
 * following compensate methods were taken directly from BME280 (BMP280) datasheet.
 * float precision gives best accuracy.
 */

// Returns temperature in DegC, double precision. Output value of “51.23” equals 51.23 DegC.
// t_fine carries fine temperature as global value
static double BME280_compensate_T_double(BME280_S32_t adc_T) {
	double var1, var2, T;
	var1 = (((double) adc_T) / 16384.0 - ((double)dig_T1)/1024.0) * ((double)dig_T2);
	var2 = ((((double) adc_T) / 131072.0 - ((double)dig_T1)/8192.0) *
	(((double)adc_T)/131072.0 - ((double) dig_T1)/8192.0)) * ((double)dig_T3);
	t_fine = (BME280_S32_t) (var1 + var2);
	T = (var1 + var2) / 5120.0;
	return T;
}
// Returns pressure in Pa as double. Output value of “96386.2” equals 96386.2 Pa = 963.862 hPa
static double BME280_compensate_P_double(BME280_S32_t adc_P) {
	double var1, var2, p;
	var1 = ((double) t_fine / 2.0) - 64000.0;
	var2 = var1 * var1 * ((double) dig_P6) / 32768.0;
	var2 = var2 + var1 * ((double) dig_P5) * 2.0;
	var2 = (var2 / 4.0) + (((double) dig_P4) * 65536.0);
	var1 = (((double) dig_P3) * var1 * var1 / 524288.0
			+ ((double) dig_P2) * var1) / 524288.0;
	var1 = (1.0 + var1 / 32768.0) * ((double) dig_P1);
	if (var1 == 0.0) {
		return 0; // avoid exception caused by division by zero
	}
	p = 1048576.0 - (double)adc_P;
	p = (p - (var2 / 4096.0)) * 6250.0 / var1;
	var1 = ((double) dig_P9) * p * p / 2147483648.0;
	var2 = p * ((double) dig_P8) / 32768.0;
	p = p + (var1 + var2 + ((double) dig_P7)) / 16.0;
	return p;
}

// Returns humidity in %rH as as double. Output value of “46.332” represents 46.332 %rH
static double BME280_compensate_H_double(BME280_S32_t adc_H) {
	double var_H;
	var_H = (((double)t_fine) - 76800.0);
	var_H = (adc_H - (((double)dig_H4) * 64.0 + ((double)dig_H5) / 16384.0 * var_H)) *
	(((double)dig_H2) / 65536.0 * (1.0 + ((double)dig_H6) / 67108864.0 * var_H *
					(1.0 + ((double)dig_H3) / 67108864.0 * var_H)));
	var_H = var_H * (1.0 - ((double)dig_H1) * var_H / 524288.0);
	if (var_H > 100.0)
	var_H = 100.0;
	else if (var_H < 0.0)
	var_H = 0.0;
	return var_H;
}

esp_err_t BME280_read(){
    if(operation_mode == BME280_MODE_FORCED){
    	if(trigger_force_read() != ESP_OK){
    		return ESP_FAIL;
    	}
    }
    uint8_t data[8];
    if (read_i2c(0xF7,data,8) != ESP_OK) {
    	return ESP_FAIL;
    }

	//0xF7 - pressure
    BME280_S32_t pres_raw =  (data[0] << 12) | (data[1] << 4) | (data[2] >> 4);
	//0xFA - temp
    BME280_S32_t temp_raw = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);
	//0xFD - humidity
    BME280_S32_t hum_raw  = (data[6] << 8) | data[7];

    result.temp = BME280_compensate_T_double(temp_raw); //Celsius
    result.pressure = BME280_compensate_P_double(pres_raw) / 100.0;  //hPA
    result.humidity = BME280_compensate_H_double(hum_raw);// pct

    return ESP_OK;
}

env_data BME280_last_result() {
	return result;
}

esp_err_t BME280_verify_chip() {
	uint8_t chipID = 0;
	uint8_t attemp = 0;
	while (attemp++ <= 5 || read_i2c(BME280_CHIP_ID_REG,&chipID,1) != ESP_OK) {
		vTaskDelay(20/portTICK_PERIOD_MS);
	}

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

esp_err_t BME280_init(uint8_t _operation_mode, uint8_t _i2c_num, uint8_t _device_addr)
{
	operation_mode = _operation_mode;
	i2c_num = _i2c_num;
	device_addr = _device_addr;

	if (BME280_verify_chip() != ESP_OK) {
		return ESP_FAIL;
	}


	BME280_writeConfigRegisters();

	BME280_readCalibrationRegisters();

	return ESP_OK;
}


