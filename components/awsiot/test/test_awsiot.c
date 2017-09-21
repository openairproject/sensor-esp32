#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include "esp_log.h"
#include <string.h>
#include "unity.h"
#include "oap_storage.h"
#include "awsiot.h"
#include "oap_test.h"

static const char* TAG = "test_awsiot";

extern const uint8_t device_cert_pem_start[] asm("_binary_1cbf751210_certificate_pem_crt_start");
extern const uint8_t device_cert_pem_end[]   asm("_binary_1cbf751210_certificate_pem_crt_end");

extern const uint8_t device_pkey_pem_start[] asm("_binary_1cbf751210_private_pem_key_start");
extern const uint8_t device_pkey_pem_end[]   asm("_binary_1cbf751210_private_pem_key_end");

static esp_err_t publish() {
	oap_meas meas = {
		.local_time = 1505156826
	};

	oap_sensor_config_t sensor_config = {
		.0
	};
	return awsiot_send(&meas, &sensor_config);
}

static void setup() {
	awsiot_config_t* awsiot_config = get_awsiot_config();
	awsiot_config->thingName="test_device_1";
	awsiot_config->endpoint="a32on3oilq3poc.iot.eu-west-1.amazonaws.com";
	awsiot_config->port=8443;
	awsiot_config->cert=str_make((void*)device_cert_pem_start, device_cert_pem_end-device_cert_pem_start);
	awsiot_config->pkey=str_make((void*)device_pkey_pem_start, device_pkey_pem_end-device_pkey_pem_start);

}


TEST_CASE("publish results to awsiot", "awsiot")
{
	setup();
	test_require_wifi();
	size_t curr_heap = 0;
	size_t prev_heap = 0;

	/*
	 * heap consumption goes to 0 after ~40 requests
	 */
	for (int i = 0; i < 1; i++) {
		curr_heap = xPortGetFreeHeapSize();
		ESP_LOGW(TAG, "REQUEST %d (heap %u,  %d bytes)", i, curr_heap, curr_heap-prev_heap);
		prev_heap = curr_heap;
		TEST_ESP_OK(publish());
		if (i) delay(1000);
	}

	free(get_awsiot_config()->cert);
	free(get_awsiot_config()->pkey);
}
