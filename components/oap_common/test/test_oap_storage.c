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
#include "oap_storage.h"

static const char* TAG = "test_oap_storage";

esp_err_t storage_get_blob(const char* key, void** out_value, size_t* length);
void storage_set_blob(const char* key, void* value, size_t length);
void storage_set_bigblob(const char* key, void* value, size_t length);
esp_err_t storage_get_bigblob(const char* key, void** out_value, size_t* length);
void storage_clean();

static const size_t MAX_NVS_VALUE_SIZE = 32 * (126 / 2 - 1);

//TEST_CASE("basic types", "[types]")
//{
//	int32_t v_32 = 0;
//	TEST_ASSERT_EQUAL_INT32(4, sizeof(v_32));
//
//	size_t v_size = 0;
//	TEST_ASSERT_EQUAL_INT32(4, sizeof(v_size));
//}

void nvs_clean() {
    ESP_LOGW(TAG, "erasing nvs");
	const esp_partition_t* nvs_partition = esp_partition_find_first(
				ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
	assert(nvs_partition && "partition table must have an NVS partition");
	ESP_ERROR_CHECK( esp_partition_erase_range(nvs_partition, 0, nvs_partition->size) );

	esp_err_t err = nvs_flash_init();
	ESP_ERROR_CHECK( err );
}

/*
TEST_CASE("nvs", "[oap_common]")
{
	nvs_clean();

	size_t blob_size = MAX_NVS_VALUE_SIZE;
	uint8_t* blob = malloc(blob_size);
	storage_put_blob("blob", blob, blob_size);
	free(blob);

	size_t str_size = MAX_NVS_VALUE_SIZE-1;
	char* str = malloc(blob_size);
	memset(str, 'a', str_size);
	str[str_size] = 0;
	storage_put_str("str", str);
	free(str);
}*/

TEST_CASE("blob", "[oap_common]")
{
	storage_clean();

	void* blob1;
    size_t size1;
    TEST_ESP_ERR(ESP_ERR_NVS_NOT_FOUND, storage_get_blob("smallblob", &blob1, &size1));

    size1 = 100;
    blob1 = malloc(size1);
    storage_set_blob("smallblob", blob1, size1);

    void* blob2;
    size_t size2;
    TEST_ESP_OK(storage_get_blob("smallblob", &blob2, &size2));
    TEST_ASSERT_EQUAL_UINT32(size1, size2);
    TEST_ASSERT_EQUAL_MEMORY(blob1, blob2, size1);

    void* blob3;
    TEST_ESP_OK(storage_get_blob("smallblob", &blob3, NULL));
    TEST_ASSERT_EQUAL_MEMORY(blob1, blob3, size1);

    free(blob1);
    free(blob2);
    free(blob3);
}

TEST_CASE("bigblob", "[oap_common]")
{
	storage_clean();

	size_t blob_size = MAX_NVS_VALUE_SIZE * 3 + 10;
	void* blob = malloc(blob_size);

	TEST_ESP_ERR(ESP_ERR_NVS_NOT_FOUND, storage_get_bigblob("blob", &blob, NULL));

	storage_set_bigblob("blob", blob, blob_size);

	void* blob2;
	size_t blob2_size;
	TEST_ESP_OK(storage_get_bigblob("blob", &blob2, &blob2_size));
	TEST_ASSERT_EQUAL_UINT32(blob_size, blob2_size);
	TEST_ASSERT_EQUAL_MEMORY(blob, blob2, blob_size);

	void* blob3;
	TEST_ESP_OK(storage_get_bigblob("blob", &blob3, NULL));
	TEST_ASSERT_EQUAL_MEMORY(blob, blob3, blob_size);

	storage_set_bigblob("blob", blob, MAX_NVS_VALUE_SIZE + 1);

	TEST_ESP_ERR(ESP_ERR_NVS_NOT_FOUND, storage_get_blob("blob.2", blob2, NULL));

	free(blob);
	free(blob2);
	free(blob3);
}

TEST_CASE("get default config", "[oap_common]")
{
	storage_clean();
    storage_init();
    cJSON* config = storage_get_config("wifi");
    TEST_ASSERT_NOT_NULL(config);
    cJSON* ssid = cJSON_GetObjectItem(config, "ssid");
    TEST_ASSERT_NOT_NULL(ssid);
    TEST_ASSERT_EQUAL_STRING("", ssid->valuestring);

    void* config_str;
    TEST_ESP_OK(storage_get_blob("config.0", &config_str, NULL));
    free(config_str);
}

TEST_CASE("get old config", "[oap_common]")
{
	storage_clean();

	char* old_config = "{ \"wifi\" : {\"ssid\":\"old\"} }";
    storage_set_blob("config", old_config, strlen(old_config)+1);

	storage_init();

    cJSON* config = storage_get_config("wifi");
    TEST_ASSERT_NOT_NULL(config);
    cJSON* ssid = cJSON_GetObjectItem(config, "ssid");
    TEST_ASSERT_NOT_NULL(ssid);
    TEST_ASSERT_EQUAL_STRING("old", ssid->valuestring);

    void* config_str;
    TEST_ESP_ERR(ESP_ERR_NVS_NOT_FOUND, storage_get_blob("config.0", &config_str, NULL));

    //save and check if saved as bigblob
    TEST_ESP_OK(storage_set_config_str(old_config));
    TEST_ESP_OK(storage_get_blob("config.0", &config_str, NULL));
    free(config_str);
    TEST_ESP_ERR(ESP_ERR_NVS_NOT_FOUND, storage_get_blob("config", &config_str, NULL));
}

TEST_CASE("get/set config", "[oap_common]")
{
	storage_clean();
	storage_init();

	cJSON* config = storage_get_config(NULL);
	TEST_ASSERT_NOT_NULL(config);

	//update config
	size_t foo_size = 2000;
	char* foo = malloc(foo_size+1);
	memset(foo, 'x', foo_size);
	foo[foo_size] = 0;

	cJSON_AddStringToObject(config, "foo", foo);
	char* config_str = cJSON_Print(config);
	TEST_ESP_OK(storage_set_config_str(config_str));
	free(config_str);

	//re-init
	ESP_LOGI(TAG, "re-init");
	storage_init();
	config = storage_get_config(NULL);
	TEST_ASSERT_NOT_NULL(config);
	TEST_ASSERT_EQUAL_STRING(foo, cJSON_GetObjectItem(config, "foo")->valuestring);

	free(foo);
}

//TEST_CASE("preserve wifi password", "oap_common")
//{
//	storage_clean();
//	storage_init();
//
//	cJSON* config = storage_get_config(NULL);
//	char* wifi_pass = "new_wifi_password";
//
//	cJSON_AddStringToObject(cJSON_GetObjectItem(config, "wifi"), "pass", wifi_pass);
//	storage_set_config_str(cJSON_Print());
//}
