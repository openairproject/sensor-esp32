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
#include "oap_common.h"

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

static uint8_t osrs_t = 1;             //Temperature oversampling x 1
static uint8_t osrs_p = 1;             //Pressure oversampling x 1
static uint8_t osrs_h = 1;             //Humidity oversampling x 1

static uint8_t t_sb = 4;               //Tstandby, 5=1000ms, 4=500ms
static uint8_t filter = 0;             //Filter off
static uint8_t spi3w_en = 0;           //3-wire SPI Disable

typedef struct temp_reading_t {
	double temp;
	BME280_S32_t t_fine;
} temp_reading_t;

static char* TAG = "i2c_bmx280";

#define CONT_IF_I2C_OK(log, comm, x)   do { esp_err_t rc = (x); if (rc != ESP_OK) { ESP_LOGW(TAG, "[%x] err: %d (%s)",comm->device_addr, rc, log); if (cmd) i2c_cmd_link_delete(cmd); return rc;} } while(0);
#define CONT_IF_OK(x)   do { esp_err_t rc = (x); if (rc != ESP_OK) return rc; } while(0);

static esp_err_t read_i2c(i2c_comm_t* comm, uint8_t reg, uint8_t* data, int len) {
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	//set address
	CONT_IF_I2C_OK("r1", comm, i2c_master_start(cmd));
	CONT_IF_I2C_OK("r2", comm, i2c_master_write_byte(cmd, (comm->device_addr << 1) | I2C_MASTER_WRITE, 1));
	CONT_IF_I2C_OK("r3", comm, i2c_master_write_byte(cmd, reg, 1));
	CONT_IF_I2C_OK("r4", comm, i2c_master_stop(cmd));
	CONT_IF_I2C_OK("r5", comm, i2c_master_cmd_begin(comm->i2c_num, cmd, 1000/portTICK_PERIOD_MS)); //often ESP_FAIL (no ack received)
	i2c_cmd_link_delete(cmd);
	cmd = 0;

	//we need to read one byte per command (see below)
	for (int i=0;i<len;i++) {
		cmd = i2c_cmd_link_create();
		CONT_IF_I2C_OK("r6", comm, i2c_master_start(cmd));
		CONT_IF_I2C_OK("r7", comm, i2c_master_write_byte(cmd, (comm->device_addr << 1) | I2C_MASTER_READ, 1));
		CONT_IF_I2C_OK("r8", comm, i2c_master_read(cmd,data+i,1,1)); //ACK is must!
		CONT_IF_I2C_OK("r9", comm, i2c_master_stop(cmd));
		CONT_IF_I2C_OK("r10",comm, i2c_master_cmd_begin(comm->i2c_num, cmd, 2000/portTICK_PERIOD_MS));
		i2c_cmd_link_delete(cmd);
		cmd = 0;
	}
	return ESP_OK;
}

static esp_err_t write_i2c_byte(i2c_comm_t *comm, uint8_t reg, uint8_t value) {
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	CONT_IF_I2C_OK("w1",comm, i2c_master_start(cmd));
	CONT_IF_I2C_OK("w2",comm, i2c_master_write_byte(cmd, (comm->device_addr << 1) | I2C_MASTER_WRITE, 1));
	CONT_IF_I2C_OK("w3",comm, i2c_master_write_byte(cmd, reg, 1));
	CONT_IF_I2C_OK("w4",comm, i2c_master_write_byte(cmd, value, 1));
	CONT_IF_I2C_OK("w5",comm, i2c_master_stop(cmd));
	CONT_IF_I2C_OK("w6",comm, i2c_master_cmd_begin(comm->i2c_num, cmd, 1000/portTICK_PERIOD_MS));
	i2c_cmd_link_delete(cmd);
	return ESP_OK;
}

static esp_err_t BME280_writeConfigRegisters(i2c_comm_t* comm, uint8_t operation_mode) {
    uint8_t ctrl_meas_reg = (osrs_t << 5) | (osrs_p << 2) | operation_mode;
    uint8_t ctrl_hum_reg  = osrs_h;

    uint8_t config_reg    = (t_sb << 5) | (filter << 2) | spi3w_en; //500ms interval

    return write_i2c_byte(comm, BME280_REG_CTRL_HUM, ctrl_hum_reg) == ESP_OK &&
    	   write_i2c_byte(comm, BME280_REG_CTRL_MEAS, ctrl_meas_reg) == ESP_OK &&
		   write_i2c_byte(comm, BME280_REG_CONFIG, config_reg) == ESP_OK ? ESP_OK : ESP_FAIL;
}

static esp_err_t trigger_force_read(i2c_comm_t* comm, uint8_t operation_mode) {
    uint8_t ctrl_meas_reg = (osrs_t << 5) | (osrs_p << 2) | operation_mode;
    if (write_i2c_byte(comm, BME280_REG_CTRL_MEAS, ctrl_meas_reg) != ESP_OK) return ESP_FAIL;
    delay(10); // wait 10ms for worst case max sensor read time
	return ESP_OK;
}

static esp_err_t BME280_readCalibrationRegisters(i2c_comm_t* comm, bmx280_calib_t* calib){
	//temp & pressure
	// Read section 0x88
	uint8_t data[24];
	if (read_i2c(comm,0x88,data,24) != ESP_OK) return ESP_FAIL;

	calib->dig_T1 = (data[1] << 8) | data[0];
	calib->dig_T2 = (data[3] << 8) | data[2];
	calib->dig_T3 = (data[5] << 8) | data[4];
	calib->dig_P1 = (data[7] << 8) | data[6];
	calib->dig_P2 = (data[9] << 8) | data[8];
	calib->dig_P3 = (data[11] << 8) | data[10];
	calib->dig_P4 = (data[13] << 8) | data[12];
	calib->dig_P5 = (data[15] << 8) | data[14];
	calib->dig_P6 = (data[17] << 8) | data[16];
	calib->dig_P7 = (data[19] << 8) | data[18];
	calib->dig_P8 = (data[21] << 8) | data[20];
	calib->dig_P9 = (data[23] << 8) | data[22];

	//humidity
	// Read section 0xA1
	if (read_i2c(comm, 0xA1,data,1) != ESP_OK) return ESP_FAIL;
	calib->dig_H1 = data[0];
	// Read section 0xE1
	if (read_i2c(comm, 0xE1,data,7) != ESP_OK) return ESP_FAIL;

	calib->dig_H2 = (data[1] << 8) | data[0];
	calib->dig_H3 = data[2];
	calib->dig_H4 = (data[3] << 4) | (0x0f & data[4]);
	calib->dig_H5 = (data[5] << 4) | ((data[4] >> 4) & 0x0F);
	calib->dig_H6 = data[6];
	return ESP_OK;
}

/**
 * following compensate methods were taken directly from BME280 (BMP280) datasheet.
 * float precision gives best accuracy.
 */

// Returns temperature in DegC, double precision. Output value of “51.23” equals 51.23 DegC.
// t_fine carries fine temperature as global value
static temp_reading_t BME280_compensate_T_double(bmx280_calib_t* calib, BME280_S32_t adc_T) {
	double var1, var2;
	var1 = (((double) adc_T) / 16384.0 - ((double)calib->dig_T1)/1024.0) * ((double)calib->dig_T2);
	var2 = ((((double) adc_T) / 131072.0 - ((double)calib->dig_T1)/8192.0) *
	(((double)adc_T)/131072.0 - ((double) calib->dig_T1)/8192.0)) * ((double)calib->dig_T3);

	temp_reading_t reading = {
			.temp = (var1 + var2) / 5120.0,
			.t_fine = (BME280_S32_t) (var1 + var2)
	};
	return reading;
}
// Returns pressure in Pa as double. Output value of “96386.2” equals 96386.2 Pa = 963.862 hPa
static double BME280_compensate_P_double(bmx280_calib_t* calib, BME280_S32_t t_fine, BME280_S32_t adc_P) {
	double var1, var2, p;
	var1 = ((double) t_fine / 2.0) - 64000.0;
	var2 = var1 * var1 * ((double) calib->dig_P6) / 32768.0;
	var2 = var2 + var1 * ((double) calib->dig_P5) * 2.0;
	var2 = (var2 / 4.0) + (((double) calib->dig_P4) * 65536.0);
	var1 = (((double) calib->dig_P3) * var1 * var1 / 524288.0
			+ ((double) calib->dig_P2) * var1) / 524288.0;
	var1 = (1.0 + var1 / 32768.0) * ((double) calib->dig_P1);
	if (var1 == 0.0) {
		return 0; // avoid exception caused by division by zero
	}
	p = 1048576.0 - (double)adc_P;
	p = (p - (var2 / 4096.0)) * 6250.0 / var1;
	var1 = ((double) calib->dig_P9) * p * p / 2147483648.0;
	var2 = p * ((double) calib->dig_P8) / 32768.0;
	p = p + (var1 + var2 + ((double) calib->dig_P7)) / 16.0;
	return p;
}

// Returns humidity in %rH as as double. Output value of “46.332” represents 46.332 %rH
static double BME280_compensate_H_double(bmx280_calib_t* calib, BME280_S32_t t_fine, BME280_S32_t adc_H) {
	double var_H;
	var_H = (((double)t_fine) - 76800.0);
	var_H = (adc_H - (((double)calib->dig_H4) * 64.0 + ((double)calib->dig_H5) / 16384.0 * var_H)) *
	(((double)calib->dig_H2) / 65536.0 * (1.0 + ((double)calib->dig_H6) / 67108864.0 * var_H *
					(1.0 + ((double)calib->dig_H3) / 67108864.0 * var_H)));
	var_H = var_H * (1.0 - ((double)calib->dig_H1) * var_H / 524288.0);
	if (var_H > 100.0)
	var_H = 100.0;
	else if (var_H < 0.0)
	var_H = 0.0;
	return var_H;
}

esp_err_t BME280_read(bme280_sensor_t* bme280_sensor, env_data_t* result){
    if(bme280_sensor->operation_mode == BME280_MODE_FORCED){
    	if(trigger_force_read(&bme280_sensor->i2c_comm, bme280_sensor->operation_mode) != ESP_OK){
    		return ESP_FAIL;
    	}
    }
    uint8_t data[8];
    if (read_i2c(&bme280_sensor->i2c_comm, 0xF7,data,8) != ESP_OK) {
    	return ESP_FAIL;
    }

	//0xF7 - pressure
    BME280_S32_t pres_raw =  (data[0] << 12) | (data[1] << 4) | (data[2] >> 4);
	//0xFA - temp
    BME280_S32_t temp_raw = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);
	//0xFD - humidity
    BME280_S32_t hum_raw  = (data[6] << 8) | data[7];


    temp_reading_t temp_reading = BME280_compensate_T_double(&bme280_sensor->calib, temp_raw);
    result->temp =  temp_reading.temp;//Celsius
    result->pressure = BME280_compensate_P_double(&bme280_sensor->calib, temp_reading.t_fine, pres_raw) / 100.0;  //hPA
    if (bme280_sensor->chip_type == CHIP_TYPE_BME) {
    	result->humidity = BME280_compensate_H_double(&bme280_sensor->calib, temp_reading.t_fine, hum_raw);// pct
    } else {
    	result->humidity = HUMIDITY_MEAS_UNSUPPORTED;
    }

    return ESP_OK;
}

esp_err_t BME280_verify_chip(bme280_sensor_t* bme280_sensor) {
	uint8_t chipID = 0;
	uint8_t attempt = 0;
	while (read_i2c(&bme280_sensor->i2c_comm, BME280_CHIP_ID_REG, &chipID,1) != ESP_OK && attempt++ < 5) {
		ESP_LOGW(TAG, "[%x] failed to read chip id (attempt %d)", bme280_sensor->i2c_comm.device_addr, attempt)
		vTaskDelay(20/portTICK_PERIOD_MS);
	}

	switch (chipID) {
		case BME280_CHIP_ID:
			bme280_sensor->chip_type=CHIP_TYPE_BME;
			ESP_LOGI(TAG,"[%x] detected BME280 (0x%X)", bme280_sensor->i2c_comm.device_addr, chipID);
			return ESP_OK;
		case BMP280_CHIP_ID1:
		case BMP280_CHIP_ID2:
		case BMP280_CHIP_ID3:
			bme280_sensor->chip_type=CHIP_TYPE_BMP;
			ESP_LOGI(TAG,"[%x] detected BMP280 - no humidity data (0x%X)", bme280_sensor->i2c_comm.device_addr, chipID);
			return ESP_OK;
		default:
			ESP_LOGW(TAG,"[%x] detected unknown chip (0x%X), disable env data", bme280_sensor->i2c_comm.device_addr, chipID);
			return ESP_FAIL;
	}
}

esp_err_t BME280_init(bme280_sensor_t* bme280_sensor)
{
	esp_err_t res;
	if ((res = BME280_verify_chip(bme280_sensor)) != ESP_OK) {
		return res;
	}
	if ((res=BME280_writeConfigRegisters(&bme280_sensor->i2c_comm, bme280_sensor->operation_mode)) != ESP_OK) {
		return res;
	}
	if ((res=BME280_readCalibrationRegisters(&bme280_sensor->i2c_comm, &bme280_sensor->calib)) != ESP_OK) {
		return res;
	}

	return ESP_OK;
}


