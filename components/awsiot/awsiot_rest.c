/*
 * aws_iot_rest.c
 *
 *  Created on: Feb 18, 2017
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

/* HTTPS GET Example using plain mbedTLS sockets
 *
 * Contacts the howsmyssl.com API via TLS v1.2 and reads a JSON
 * response.
 *
 * Adapted from the ssl_client1 example in mbedtls.
 *
 * Original Copyright (C) 2006-2016, ARM Limited, All Rights Reserved, Apache 2.0 License.
 * Additions Copyright (C) Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD, Apache 2.0 License.
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "mbedtls/platform.h"
#include "mbedtls/net.h"
#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"

#include "awsiot_rest.h"

#define WEB_SERVER "a32on3oilq3poc.iot.eu-west-1.amazonaws.com"
#define WEB_PORT "8443"
#define WEB_URL "/things/pm_wro_2/shadow"

static const char *TAG = "awsiot";


/* Root cert for howsmyssl.com, taken from server_root_cert.pem

   The PEM file was extracted from the output of this command:
   openssl s_client -showcerts -connect www.howsmyssl.com:443 </dev/null

   The CA root cert is the last cert given in the chain of certs.

   To embed it in the app binary, the PEM file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
*/
extern const uint8_t verisign_root_ca_pem_start[] asm("_binary_verisign_root_ca_pem_start");
extern const uint8_t verisign_root_ca_pem_end[]   asm("_binary_verisign_root_ca_pem_end");


#ifdef MBEDTLS_DEBUG_C

#define MBEDTLS_DEBUG_LEVEL 1

/* mbedtls debug function that translates mbedTLS debug output
   to ESP_LOGx debug output.

   MBEDTLS_DEBUG_LEVEL 4 means all mbedTLS debug output gets sent here,
   and then filtered to the ESP logging mechanism.
*/
static void mbedtls_debug(void *ctx, int level,
                     const char *file, int line,
                     const char *str)
{
    const char *MBTAG = "mbedtls";
    char *file_sep;

    /* Shorten 'file' from the whole file path to just the filename

       This is a bit wasteful because the macros are compiled in with
       the full _FILE_ path in each case.
    */
    file_sep = rindex(file, '/');
    if(file_sep)
        file = file_sep+1;

    switch(level) {
    case 1:
        ESP_LOGI(MBTAG, "%s:%d %s", file, line, str);
        break;
    case 2:
    case 3:
        ESP_LOGD(MBTAG, "%s:%d %s", file, line, str);
        break;
    case 4:
        ESP_LOGV(MBTAG, "%s:%d %s", file, line, str);
        break;
    default:
        ESP_LOGE(MBTAG, "Unexpected log level %d: %s", level, str);
        break;
    }
}

#endif


static esp_err_t make_request(mbedtls_ssl_context ssl_ctx, char* body) {
	char buf[1024];
	int len,flags;

	esp_err_t ret;
	mbedtls_net_context server_fd;
	mbedtls_net_init(&server_fd);



	ESP_LOGI(TAG, "Connecting to %s:%s...", WEB_SERVER, WEB_PORT);

	if ((ret = mbedtls_net_connect(&server_fd, WEB_SERVER,
								  WEB_PORT, MBEDTLS_NET_PROTO_TCP)) != ESP_OK)
	{
		ESP_LOGE(TAG, "mbedtls_net_connect returned -%x", -ret);
		goto request_complete;
	}

	ESP_LOGI(TAG, "Connected.");

	mbedtls_ssl_set_bio(&ssl_ctx, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

	ESP_LOGI(TAG, "Performing the SSL/TLS handshake...");

	while ((ret = mbedtls_ssl_handshake(&ssl_ctx)) != ESP_OK)
	{
		if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
		{
			ESP_LOGE(TAG, "mbedtls_ssl_handshake returned -0x%x", -ret);
			goto request_complete;
		}
	}

	ESP_LOGI(TAG, "Verifying peer X.509 certificate...");

	if ((flags = mbedtls_ssl_get_verify_result(&ssl_ctx)) != ESP_OK)
	{
		ESP_LOGW(TAG, "Failed to verify peer certificate!");
		bzero(buf, sizeof(buf));
		mbedtls_x509_crt_verify_info(buf, sizeof(buf), "  ! ", flags);
		ESP_LOGW(TAG, "verification info: %s", buf);
		goto request_complete;
	}
	else {
		ESP_LOGI(TAG, "Certificate verified.");
	}

	ESP_LOGI(TAG, "Writing HTTP request...");

	char request[1024];

	sprintf(request, "POST %s HTTP/1.1\n"
	    "Host: %s\n"
		"Content-Type: application/json\n"
		"Connection: close\n"
		"Content-Length: %d\n"
	    "\r\n%s", WEB_URL, WEB_SERVER, strlen(body), body);

	while((ret = mbedtls_ssl_write(&ssl_ctx, (unsigned char*)request, strlen(request))) <= 0)
	{
		if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
		{
			ESP_LOGE(TAG, "mbedtls_ssl_write returned -0x%x", -ret);
			goto request_complete;
		}
	}

	len = ret;
	ESP_LOGI(TAG, "%d bytes written", len);
	ESP_LOGI(TAG, "Reading HTTP response...");

	//	leaving response unread causes crash.
	//	I guess non-blocking read func is being invoked despite destroyed ssl context
	//	goto request_complete;


	struct timeval timeout = {.tv_sec = 1};
	setsockopt(server_fd.fd , SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(struct timeval));

    do
    {
        len = sizeof(buf) - 1;
        bzero(buf, sizeof(buf));
        ret = mbedtls_ssl_read(&ssl_ctx, (unsigned char *)buf, len);

        if(ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        	ESP_LOGD(TAG, "ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE");
            continue;
        }

        if(ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            ret = 0;
            break;
        }

        if(ret < 0)
        {
            ESP_LOGE(TAG, "mbedtls_ssl_read returned -0x%x", -ret);
            break;
        }

        if(ret == 0)
        {
            ESP_LOGI(TAG, "connection closed");
            break;
        }

        len = ret;
        ESP_LOGI(TAG, "%d bytes read", len);
        /* Print response directly to stdout as it is read */
        for(int i = 0; i < len; i++) {
            putchar(buf[i]);
        }
    } while(1);

	request_complete:
    	ESP_LOGD(TAG,"mbedtls_ssl_close_notify");
    	mbedtls_ssl_close_notify(&ssl_ctx);

		ESP_LOGD(TAG,"mbedtls_net_free");
		mbedtls_net_free(&server_fd);
		return ret;
}


esp_err_t awsiot_update_shadow(awsiot_config_t awsiot_config, char* body)
{
    int ret;

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context ssl_ctx;
    mbedtls_ssl_config ssl_conf;
    mbedtls_x509_crt cacert;
    mbedtls_x509_crt clicert;
    mbedtls_pk_context pkey;

    mbedtls_x509_crt_init(&cacert);
    mbedtls_x509_crt_init(&clicert);
    mbedtls_pk_init(&pkey);

    mbedtls_ssl_init(&ssl_ctx);
    mbedtls_ssl_config_init(&ssl_conf);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);

    if((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                    NULL, 0)) != ESP_OK)
    {
        ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed returned %d", ret);
        return ret;
    }

    ESP_LOGD(TAG, "Loading the CA root certificate...");
    ret = mbedtls_x509_crt_parse(&cacert, verisign_root_ca_pem_start,
    		verisign_root_ca_pem_end-verisign_root_ca_pem_start);

    if(ret < 0)
    {
        ESP_LOGE(TAG, "mbedtls_x509_crt_parse returned -0x%x", -ret);
        return ret;
    }

    ESP_LOGI(TAG, "Setting hostname for TLS session...");
     /* Hostname set here should match CN in server certificate */
    if((ret = mbedtls_ssl_set_hostname(&ssl_ctx, WEB_SERVER)) != ESP_OK)
    {
        ESP_LOGE(TAG, "mbedtls_ssl_set_hostname returned -0x%x", -ret);
        return ret;
    }

    ESP_LOGI(TAG, "Setting up the SSL/TLS structure...");

    if((ret = mbedtls_ssl_config_defaults(&ssl_conf,
                                          MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
    {
        ESP_LOGE(TAG, "mbedtls_ssl_config_defaults returned %d", ret);
        goto exit;
    }

    //mbedtls_ssl_conf_read_timeout(&ssl_conf, 1000);

    /* MBEDTLS_SSL_VERIFY_OPTIONAL is bad for security, in this example it will print
       a warning if CA verification fails but it will continue to connect.
       You should consider using MBEDTLS_SSL_VERIFY_REQUIRED in your own code.
    */
    mbedtls_ssl_conf_authmode(&ssl_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&ssl_conf, &cacert, NULL);
    mbedtls_ssl_conf_rng(&ssl_conf, mbedtls_ctr_drbg_random, &ctr_drbg);
#ifdef MBEDTLS_DEBUG_C
    mbedtls_debug_set_threshold(MBEDTLS_DEBUG_LEVEL);
    mbedtls_ssl_conf_dbg(&ssl_conf, mbedtls_debug, NULL);
#endif

    //requires 0 at the end?
    if ((ret = mbedtls_x509_crt_parse( &clicert, (unsigned char*)awsiot_config.cert, strlen(awsiot_config.cert)+1)) != ESP_OK) {
    	ESP_LOGE(TAG, "cannot parse AWS Thing certificate (-0x%x)", -ret);
    	        goto exit;
    }
    //requires 0 at the end?
    if ((ret = mbedtls_pk_parse_key( &pkey, (unsigned char*)awsiot_config.pkey, strlen(awsiot_config.pkey)+1, NULL, 0 )) != ESP_OK) {
    	ESP_LOGE(TAG, "cannot parse AWS Thing private key (-0x%x)", -ret);
    	    	        goto exit;
    }

    if( ( ret = mbedtls_ssl_conf_own_cert( &ssl_conf, &clicert, &pkey ) ) != ESP_OK)
    {
    	ESP_LOGE(TAG, "mbedtls_ssl_conf_own_cert returned %d", ret );
        goto exit;
    }

    if ((ret = mbedtls_ssl_setup(&ssl_ctx, &ssl_conf)) != ESP_OK)
    {
        ESP_LOGE(TAG, "mbedtls_ssl_setup returned -0x%x", -ret);
        goto exit;
    }


    ret = make_request(ssl_ctx, body);


    exit:
		if(ret != 0)
		{

			ESP_LOGD(TAG,"mbedtls_strerror");
			char buf[100] = {};
			mbedtls_strerror(ret, buf, 100);
			ESP_LOGE(TAG, "Last error was: -0x%x - %s", -ret, buf);
		}

		//ESP_LOGD(TAG,"mbedtls_ssl_session_reset");
		//this fails if response wasn't fully read!
		//mbedtls_ssl_session_reset(&ssl_ctx); //reset for reuse.

    	//free for good
		ESP_LOGD(TAG,"mbedtls_x509_crt_free (cacert)");
		mbedtls_x509_crt_free(&cacert);

		ESP_LOGD(TAG,"mbedtls_x509_crt_free (clicert)");
        mbedtls_x509_crt_free(&clicert);

		ESP_LOGD(TAG,"mbedtls_pk_free");
        mbedtls_pk_free(&pkey);

		ESP_LOGD(TAG,"mbedtls_entropy_free");
        mbedtls_entropy_free(&entropy);

		ESP_LOGD(TAG,"mbedtls_ctr_drbg_free");
		mbedtls_ctr_drbg_free(&ctr_drbg);

		ESP_LOGD(TAG,"mbedtls_ssl_config_free");
		mbedtls_ssl_config_free(&ssl_conf);

		ESP_LOGD(TAG,"mbedtls_ssl_free");
    	mbedtls_ssl_free(&ssl_ctx);


        return ret;

}
