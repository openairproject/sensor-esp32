/*
 * oap_common.c
 *
 *  Created on: Feb 22, 2017
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


#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include <sys/time.h>
#include "oap_common.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/task.h"

static const long FEB22_2017 = 1487795557;
static const char *TAG="oap";

static int _reboot_in_progress = 0;
int is_reboot_in_progress() {
	return _reboot_in_progress;
}
void oap_reboot(char* cause) {
	ESP_LOGW(TAG, "REBOOT ON DEMAND (%s)", cause);
	_reboot_in_progress = 1;
	esp_restart();
}

long oap_epoch_sec() {
	struct timeval tv_start;
	gettimeofday(&tv_start, NULL);
	return tv_start.tv_sec;
}

long oap_epoch_sec_valid() {
	long epoch = oap_epoch_sec();
	return epoch > FEB22_2017 ? epoch : 0;
}

char* str_make(void* data, int len) {
	char* str = malloc(len+1);
	memcpy(str, data, len);
	str[len] = 0;
	return str;
}

char* str_dup(char* src) {
	char* dest = malloc(strlen(src)+1);
	strcpy(dest, src);
	return dest;
}

void yield()
{
    vPortYield();
}

uint32_t IRAM_ATTR micros()
{
    uint32_t ccount;
    __asm__ __volatile__ ( "rsr     %0, ccount" : "=a" (ccount) );
    return ccount / CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ;
}

uint32_t IRAM_ATTR millis()
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

void delay(uint32_t ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

void IRAM_ATTR delayMicroseconds(uint32_t us)
{
    uint32_t m = micros();
    if(us){
        uint32_t e = (m + us) % ((0xFFFFFFFF / CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ) + 1);
        if(m > e){ //overflow
            while(micros() > e){
                NOP();
            }
        }
        while(micros() < e){
            NOP();
        }
    }
}

void set_gpio(uint8_t gpio, uint8_t enabled) {
	if (gpio > 0) {
		ESP_LOGD(TAG, "set pin %d => %d", gpio, enabled);
		gpio_set_level(gpio, enabled);
	}
}

void configure_gpio(uint8_t gpio) {
	if (gpio > 0) {
		ESP_LOGD(TAG, "configure pin %d as output", gpio);
		gpio_pad_select_gpio(gpio);
		ESP_ERROR_CHECK(gpio_set_direction(gpio, GPIO_MODE_OUTPUT));
		ESP_ERROR_CHECK(gpio_set_pull_mode(gpio, GPIO_PULLDOWN_ONLY));
	}
}

void req_set_user_agent(request_t* req) {
    char agent[80];
    sprintf(agent, "User-Agent: OTA-ESP32/%s", oap_version_str());
    req_setopt(req, REQ_SET_HEADER, agent);
}

double sma_generator (sma_data_t *sma, double value) {
	sma->sum += value;
	sma->data[sma->lastpos%sma->size] = value;
	sma->lastpos++;
	if(sma->lastpos >= sma->size) {
		sma->sum -= sma->data[sma->lastpos%sma->size];
	}
	return sma->sum/sma->size;
}

