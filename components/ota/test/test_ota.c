#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include "unity.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_partition.h"
#include "esp_log.h"
#include <string.h>
#include "ota.h"
#include "bootwifi.h"
#include "oap_test.h"
#include "oap_common.h"
#include "oap_version.h"
#include "mbedtls/sha256.h"

#include "../ota_int.h"

static const char* TAG = "test_ota";

void sha_to_hexstr(unsigned char hash[32], unsigned char hex[64]);

static void TEST_ASSERT_EQUAL_VER(uint8_t expected_major, uint8_t expected_minor, uint8_t expected_patch, oap_version_t* version) {
	TEST_ASSERT_EQUAL_INT(expected_major, version->major);
	TEST_ASSERT_EQUAL_INT(expected_minor, version->minor);
	TEST_ASSERT_EQUAL_INT(expected_patch, version->patch);
}

TEST_CASE("sha","[ota]")
{
	char* input = "hello world";
	char* output = "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9";

	unsigned char hash[32];
	unsigned char hex[65];
	hex[64] = 0;

	//single value
	mbedtls_sha256((unsigned char*)input, strlen(input), hash, 0);
	sha_to_hexstr(hash, hex);
	TEST_ASSERT_EQUAL_STRING(output, hex);

	//in parts
	mbedtls_sha256_context sha_context;
	mbedtls_sha256_init(&sha_context);
	mbedtls_sha256_starts(&sha_context,0);
	for (int i = 0; i < strlen(input); i++) {
		mbedtls_sha256_update(&sha_context, (unsigned char *)input+i, 1);
	}
	mbedtls_sha256_finish(&sha_context, hash);
	mbedtls_sha256_free(&sha_context);
	sha_to_hexstr(hash, hex);
	TEST_ASSERT_EQUAL_STRING(output, hex);
}


TEST_CASE("fetch_last_ota_info", "[ota]")
{
	test_require_wifi();
	ota_config_t config = {
			.host = "https://openairproject.com",
			.path = "/ota"
	};
	ota_info_t info;
	/*
	 * heap consumption goes to 0 after ~5 requests
	 */
	size_t curr_heap = 0;
	size_t prev_heap = 0;
	for (int i = 0; i < 1; i++) {
		curr_heap = xPortGetFreeHeapSize();
		ESP_LOGW(TAG, "REQUEST %d (heap %u,  %d bytes)", i, curr_heap, curr_heap-prev_heap);
		prev_heap = curr_heap;
		TEST_ESP_OK(fetch_last_ota_info(&config, &info));
		TEST_ASSERT_EQUAL_STRING("sensor-esp32-0.4.0.bin", info.file);
		TEST_ASSERT_EQUAL_VER(0,4,0,&info.ver);
		TEST_ASSERT_EQUAL_STRING("929fd82b12f4e67cfa08a14e763232a95820b7f2b2edcce744e1c1711c7c9e04", info.sha);

		delay(1000);
	}
}

TEST_CASE("parse_ota_info","[ota]")
{
	ota_info_t info;
	char* data = "0.4.0|sensor-esp32-0.4.0.bin|929fd82b12f4e67cfa08a14e763232a95820b7f2b2edcce744e1c1711c7c9e04\r\n";
	parse_ota_info(&info, data, strlen(data));

	TEST_ASSERT_EQUAL_STRING("sensor-esp32-0.4.0.bin", info.file);
	TEST_ASSERT_EQUAL_VER(0,4,0,&info.ver);
	TEST_ASSERT_EQUAL_STRING("929fd82b12f4e67cfa08a14e763232a95820b7f2b2edcce744e1c1711c7c9e04", info.sha);
}

TEST_CASE("download_ota_binary", "[ota]")
{
	test_require_wifi();
	ota_config_t config = {
		.host = "https://openairproject.com",
		.path = "/ota"
	};

	ota_info_t info = {
		.file = "sensor-esp32-0.4.0.bin",
		.sha = "929fd82b12f4e67cfa08a14e763232a95820b7f2b2edcce744e1c1711c7c9e04"
	};

	TEST_ESP_OK(download_ota_binary(&config, &info, NULL));
}

TEST_CASE("full ota", "[ota]")
{
	test_init_wifi();
	ota_config_t ota_config = {
			.host = "https://openairproject.com",
			.path = "/ota-test",
			.min_version=400,
			.commit_and_reboot = 0,
			.update_partition = NULL,
			.interval = 0
		};
	TEST_ESP_OK(check_ota_task(&ota_config));
	TEST_ASSERT_NOT_NULL(ota_config.update_partition);
}
