#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>

#include "oap_common.h"
#undef HTTP_GET
#undef HTTP_POST
#include "cJSON.h"
#include "oap_storage.h"
#include "pm_meter.h"
#include "mhz19.h"
#include "hw_gpio.h"

#include "web.h"

static const char *TAG="web";
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

static esp_err_t index_handler(httpd_req_t *req)
{
    size_t resp_size = index_html_end-index_html_start;
    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    httpd_resp_send(req, (const char *)index_html_start, resp_size);
    return ESP_OK;
}

httpd_uri_t index_desc = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_handler
};

static esp_err_t reboot_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    httpd_resp_send(req, "", 0);
    oap_reboot("requested by user");
    return ESP_OK;
}

httpd_uri_t reboot_desc = {
    .uri       = "/reboot",
    .method    = HTTP_GET,
    .handler   = reboot_handler
};

static esp_err_t handler_get_config(httpd_req_t *req)
{
    httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    httpd_resp_set_hdr(req, "X-Version", oap_version_str());

    cJSON* config = storage_get_config_to_update();
    char* json = cJSON_Print(config);
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

httpd_uri_t get_config_desc = {
    .uri       = "/config",
    .method    = HTTP_GET,
    .handler   = handler_get_config
};

static esp_err_t handler_post_config(httpd_req_t *req)
{
    /* Read request bod */
    char content[1000];

    /* Truncate if content length larger than the buffer */
    size_t recv_size = MIN(req->content_len, sizeof(content));

    int ret = httpd_req_recv(req, content, recv_size);
    if (ret < 0) {
        /* In case of recv error, returning ESP_FAIL will
         * ensure that the underlying socket is closed */
        return ESP_FAIL;
    }
    cJSON* config = cJSON_Parse(content);
	if (config) {
		storage_update_config(config);
		handler_get_config(req);
	} else {
		httpd_resp_set_status(req, HTTPD_500);
	}
	cJSON_Delete(config);

    /* Send a simple response */
    const char *resp = "URI POST Response";
    httpd_resp_send(req, resp, strlen(resp));
 
    return ESP_OK;
}

httpd_uri_t post_config_desc = {
    .uri       = "/config",
    .method    = HTTP_POST,
    .handler   = handler_post_config
};

static esp_err_t handler_get_status(httpd_req_t *req)
{
        httpd_resp_set_type(req, HTTPD_TYPE_JSON);
	cJSON *root, *status, *data;

	root = cJSON_CreateObject();
	status = cJSON_CreateObject();
	data = cJSON_CreateObject();

	cJSON_AddItemToObject(root, "status", status);
	cJSON_AddItemToObject(root, "data", data);

	cJSON_AddItemToObject(status, "version", cJSON_CreateString(oap_version_str()));

	time_t now = time(NULL);
	struct tm timeinfo = {0};

	localtime_r(&now, &timeinfo);
	time_t boot_time = now-(xTaskGetTickCount()/configTICK_RATE_HZ);
	long sysTime = oap_epoch_sec();
        if(timeinfo.tm_year > (2016 - 1900)) {
#if 0
                setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
                tzset();
#endif                
                char strftime_buf[64];
                strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
                cJSON_AddItemToObject(status, "utctime", cJSON_CreateString(strftime_buf));
	}
	cJSON_AddItemToObject(status, "time", cJSON_CreateNumber(now));
	cJSON_AddItemToObject(status, "uptime", cJSON_CreateNumber(now - boot_time));
	cJSON_AddItemToObject(status, "heap", cJSON_CreateNumber(esp_get_free_heap_size()));
	#ifdef CONFIG_OAP_BMX280_ENABLED
	if(last_env_data[0].timestamp) {
		cJSON *envobj0 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env0", envobj0);
		cJSON_AddItemToObject(envobj0, "temp", cJSON_CreateNumber(last_env_data[0].env_data.bmx280.temp));
		cJSON_AddItemToObject(envobj0, "pressure", cJSON_CreateNumber(last_env_data[0].env_data.bmx280.sealevel));	
		cJSON_AddItemToObject(envobj0, "humidity", cJSON_CreateNumber(last_env_data[0].env_data.bmx280.humidity));
		cJSON_AddItemToObject(envobj0, "timestamp", cJSON_CreateNumber(sysTime - last_env_data[0].timestamp));
	}
	#endif
	#ifdef CONFIG_OAP_BMX280_ENABLED_AUX
	if(last_env_data[1].timestamp) {
		cJSON *envobj1 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env1", envobj1);
		cJSON_AddItemToObject(envobj1, "temp", cJSON_CreateNumber(last_env_data[1].env_data.bmx280.temp));
		cJSON_AddItemToObject(envobj1, "pressure", cJSON_CreateNumber(last_env_data[1].env_data.bmx280.sealevel));	
		cJSON_AddItemToObject(envobj1, "humidity", cJSON_CreateNumber(last_env_data[1].env_data.bmx280.humidity));
		cJSON_AddItemToObject(envobj1, "timestamp", cJSON_CreateNumber(sysTime - last_env_data[1].timestamp));	
	}
	#endif
	#ifdef CONFIG_OAP_MH_ENABLED
	if(last_env_data[2].timestamp) {
		cJSON *envobj2 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env2", envobj2);
		cJSON_AddItemToObject(envobj2, "co2", cJSON_CreateNumber(last_env_data[2].env_data.mhz19.co2));
		cJSON_AddItemToObject(envobj2, "timestamp", cJSON_CreateNumber(sysTime - last_env_data[2].timestamp));	
	}
	#endif
	#ifdef CONFIG_OAP_HCSR04_0_ENABLED 
	if(last_env_data[3].timestamp) {
		cJSON *envobj3 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env3", envobj3);
		cJSON_AddItemToObject(envobj3, "distance", cJSON_CreateNumber(last_env_data[3].env_data.hcsr04.distance));
		cJSON_AddItemToObject(envobj3, "timestamp", cJSON_CreateNumber(sysTime - last_env_data[3].timestamp));	
	}
	#endif
	#ifdef CONFIG_OAP_HCSR04_1_ENABLED
	if(last_env_data[4].timestamp) {
		cJSON *envobj4 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env4", envobj4);
		cJSON_AddItemToObject(envobj4, "distance", cJSON_CreateNumber(last_env_data[4].env_data.hcsr04.distance));
		cJSON_AddItemToObject(envobj4, "timestamp", cJSON_CreateNumber(sysTime - last_env_data[4].timestamp));	
	}
	#endif
	#ifdef CONFIG_OAP_GPIO_0_ENABLED
	if(last_env_data[5].timestamp) {
		cJSON *envobj5 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env5", envobj5);
		cJSON_AddItemToObject(envobj5, "GPIlastLow", cJSON_CreateNumber(last_env_data[5].env_data.gpio.GPIlastLow));
		cJSON_AddItemToObject(envobj5, "GPIlastHigh", cJSON_CreateNumber(last_env_data[5].env_data.gpio.GPIlastHigh));
		cJSON_AddItemToObject(envobj5, "GPICounter", cJSON_CreateNumber(last_env_data[5].env_data.gpio.GPICounter));
		cJSON_AddItemToObject(envobj5, "GPICountDelta", cJSON_CreateNumber(last_env_data[5].env_data.gpio.GPICountDelta));
		cJSON_AddItemToObject(envobj5, "GPOlastOut", cJSON_CreateNumber(last_env_data[5].env_data.gpio.GPOlastOut));
		cJSON_AddItemToObject(envobj5, "GPOlastVal", cJSON_CreateNumber(last_env_data[5].env_data.gpio.GPOlastVal));
		cJSON_AddItemToObject(envobj5, "GPOtriggerLength", cJSON_CreateNumber(last_env_data[5].env_data.gpio.GPOtriggerLength));
		cJSON_AddItemToObject(envobj5, "timestamp", cJSON_CreateNumber(sysTime - last_env_data[5].timestamp));	
	}
	#endif
	#ifdef CONFIG_OAP_GPIO_1_ENABLED
	if(last_env_data[6].timestamp) {
		cJSON *envobj6 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env6", envobj6);
		cJSON_AddItemToObject(envobj6, "GPIlastLow", cJSON_CreateNumber(last_env_data[6].env_data.gpio.GPIlastLow));
		cJSON_AddItemToObject(envobj6, "GPICounter", cJSON_CreateNumber(last_env_data[6].env_data.gpio.GPICounter));
		cJSON_AddItemToObject(envobj6, "GPICountDelta", cJSON_CreateNumber(last_env_data[6].env_data.gpio.GPICountDelta));
		cJSON_AddItemToObject(envobj6, "GPIlastHigh", cJSON_CreateNumber(last_env_data[6].env_data.gpio.GPIlastHigh));
		cJSON_AddItemToObject(envobj6, "GPOlastOut", cJSON_CreateNumber(last_env_data[6].env_data.gpio.GPOlastOut));
		cJSON_AddItemToObject(envobj6, "GPOlastVal", cJSON_CreateNumber(last_env_data[6].env_data.gpio.GPOlastVal));
		cJSON_AddItemToObject(envobj6, "GPOtriggerLength", cJSON_CreateNumber(last_env_data[6].env_data.gpio.GPOtriggerLength));
		cJSON_AddItemToObject(envobj6, "timestamp", cJSON_CreateNumber(sysTime - last_env_data[6].timestamp));			
	}
	#endif
	#ifdef CONFIG_OAP_GPIO_2_ENABLED
	if(last_env_data[7].timestamp) {
		cJSON *envobj7 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env7", envobj7);
		cJSON_AddItemToObject(envobj7, "GPIlastLow", cJSON_CreateNumber(last_env_data[7].env_data.gpio.GPIlastLow));
		cJSON_AddItemToObject(envobj7, "GPIlastHigh", cJSON_CreateNumber(last_env_data[7].env_data.gpio.GPIlastHigh));
		cJSON_AddItemToObject(envobj7, "GPICounter", cJSON_CreateNumber(last_env_data[7].env_data.gpio.GPICounter));
		cJSON_AddItemToObject(envobj7, "GPICountDelta", cJSON_CreateNumber(last_env_data[7].env_data.gpio.GPICountDelta));
		cJSON_AddItemToObject(envobj7, "GPOlastOut", cJSON_CreateNumber(last_env_data[7].env_data.gpio.GPOlastOut));
		cJSON_AddItemToObject(envobj7, "GPOlastVal", cJSON_CreateNumber(last_env_data[7].env_data.gpio.GPOlastVal));
		cJSON_AddItemToObject(envobj7, "GPOtriggerLength", cJSON_CreateNumber(last_env_data[7].env_data.gpio.GPOtriggerLength));
		cJSON_AddItemToObject(envobj7, "timestamp", cJSON_CreateNumber(sysTime - last_env_data[7].timestamp));	
	}
	#endif
	#ifdef CONFIG_OAP_GPIO_3_ENABLED
	if(last_env_data[8].timestamp) {
		cJSON *envobj8 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "env8", envobj8);
		cJSON_AddItemToObject(envobj8, "GPIlastLow", cJSON_CreateNumber(last_env_data[8].env_data.gpio.GPIlastLow));
		cJSON_AddItemToObject(envobj8, "GPIlastHigh", cJSON_CreateNumber(last_env_data[8].env_data.gpio.GPIlastHigh));
		cJSON_AddItemToObject(envobj8, "GPICounter", cJSON_CreateNumber(last_env_data[8].env_data.gpio.GPICounter));
		cJSON_AddItemToObject(envobj8, "GPICountDelta", cJSON_CreateNumber(last_env_data[8].env_data.gpio.GPICountDelta));
		cJSON_AddItemToObject(envobj8, "GPOlastOut", cJSON_CreateNumber(last_env_data[8].env_data.gpio.GPOlastOut));
		cJSON_AddItemToObject(envobj8, "GPOlastVal", cJSON_CreateNumber(last_env_data[8].env_data.gpio.GPOlastVal));
		cJSON_AddItemToObject(envobj8, "GPOtriggerLength", cJSON_CreateNumber(last_env_data[8].env_data.gpio.GPOtriggerLength));
		cJSON_AddItemToObject(envobj8, "timestamp", cJSON_CreateNumber(sysTime - last_env_data[8].timestamp));			
	}
	#endif
	#ifdef CONFIG_OAP_PM_UART_ENABLE
	if(pm_data_array.timestamp) {
		cJSON *pmobj0 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "pm0", pmobj0);
		cJSON_AddItemToObject(pmobj0, "pm1_0", cJSON_CreateNumber(pm_data_array.pm_data[0].pm1_0));
		cJSON_AddItemToObject(pmobj0, "pm2_5", cJSON_CreateNumber(pm_data_array.pm_data[0].pm2_5));
		cJSON_AddItemToObject(pmobj0, "pm10", cJSON_CreateNumber(pm_data_array.pm_data[0].pm10));
		cJSON_AddItemToObject(pmobj0, "timestamp", cJSON_CreateNumber(sysTime - pm_data_array.timestamp));
	#ifdef CONFIG_OAP_PM_UART_AUX_ENABLE
		cJSON *pmobj1 = cJSON_CreateObject();
		cJSON_AddItemToObject(data, "pm1", pmobj1);
		cJSON_AddItemToObject(pmobj1, "pm1_0", cJSON_CreateNumber(pm_data_array.pm_data[1].pm1_0));
		cJSON_AddItemToObject(pmobj1, "pm2_5", cJSON_CreateNumber(pm_data_array.pm_data[1].pm2_5));
		cJSON_AddItemToObject(pmobj1, "pm10", cJSON_CreateNumber(pm_data_array.pm_data[1].pm10));
	#endif
	}
	#endif
	char* json = cJSON_Print(root);
        httpd_resp_send(req, json, strlen(json));

	free(json);
	cJSON_Delete(root);
    return ESP_OK;
}

httpd_uri_t get_status_desc = {
    .uri       = "/status",
    .method    = HTTP_GET,
    .handler   = handler_get_status
};

static esp_err_t trigger_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;
    char *str = "ok\n";
    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            char param[32];
            int delay = 1000;
            int value = 0;
            int gpio = -1;
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "delay", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => delay=%s", param);
                delay=atoi(param);
            }
            if (httpd_query_key_value(buf, "value", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => value=%s", param);
                value=atoi(param);
            }
            if (httpd_query_key_value(buf, "gpio", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => gpio=%s", param);
                gpio=atoi(param);
            }
            if (httpd_query_key_value(buf, "func", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => func=%s", param);
                if(!strcasecmp(param, "calibrate")) {
			mhz19_calibrate(&mhz19_cfg[0]);
		}
            } else if(gpio >= 0) {
	        hw_gpio_queue_trigger(gpio, value, delay);
	    }
        }
        free(buf);
    }
    httpd_resp_send(req, str, strlen(str));
    return ESP_OK;
}

httpd_uri_t trigger_desc = {
    .uri       = "/trigger",
    .method    = HTTP_GET,
    .handler   = trigger_handler
};

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6*1024;
    config.lru_purge_enable = true;
    config.backlog_conn = 25;
    config.max_open_sockets = 12;
    config.max_uri_handlers = 6;
    
    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &index_desc);
        httpd_register_uri_handler(server, &reboot_desc);
        httpd_register_uri_handler(server, &get_config_desc);
        httpd_register_uri_handler(server, &post_config_desc);
        httpd_register_uri_handler(server, &get_status_desc);
        httpd_register_uri_handler(server, &trigger_desc);
        esp_log_level_set("httpd_parse", ESP_LOG_WARN);
        esp_log_level_set("httpd_txrx", ESP_LOG_WARN);
        esp_log_level_set("httpd_uri", ESP_LOG_WARN);
        esp_log_level_set("httpd_sess", ESP_LOG_WARN);
        esp_log_level_set("httpd", ESP_LOG_WARN);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}


static httpd_handle_t server = NULL;

void web_wifi_handler(bool connected, bool ap_mode)
{
        if (connected) {
                if (server == NULL) {
                        server = start_webserver();
                }
        } else {
        
                if (server) {
                        stop_webserver(server);
                        server = NULL;
                }
        }
}

