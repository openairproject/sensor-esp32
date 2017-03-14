/*
 *
 *  Created on: Feb 23, 2017
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

/* Provide SSL/TLS functions to ESP32 with Arduino IDE
*
* Adapted from the ssl_client1 example in mbedtls.
*
* Original Copyright (C) 2006-2015, ARM Limited, All Rights Reserved, Apache 2.0 License.
* Additions Copyright (C) 2017 Evandro Luis Copercini, Apache 2.0 License.
*/

//#include "Arduino.h"
#include <lwip/sockets.h>
#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>

#include "mbedtls/platform.h"
#include "mbedtls/net.h"
#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/ssl_internal.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"

#include <string.h>
#include <stdlib.h>

#include "oap_common.h"
#include "ssl_client.h"


static const char *pers = "esp32-tls";

#define DEBUG true //Set false to supress debug messages

#ifdef DEBUG
#define DEBUG_PRINT(...) printf( __VA_ARGS__ )
#else
#define DEBUG_PRINT(x)
#endif

#ifdef MBEDTLS_DEBUG_C

#define MBEDTLS_DEBUG_LEVEL 4

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
    if (file_sep) {
        file = file_sep + 1;
    }

    switch (level) {
    case 1:
        printf( "%s:%d %s \n", file, line, str);
        break;
    case 2:
    case 3:
        printf( "%s:%d %s \n", file, line, str);
    case 4:
        printf( "%s:%d %s \n", file, line, str);
        break;
    default:
        printf( "Unexpected log level %d: %s \n", level, str);
        break;
    }
}

#endif


void ssl_init(sslclient_context *ssl_client)
{
    /*
    * Initialize the RNG and the session data
    */

    mbedtls_ssl_init(&ssl_client->ssl_ctx);
    mbedtls_ssl_config_init(&ssl_client->ssl_conf);

    mbedtls_ctr_drbg_init(&ssl_client->drbg_ctx);
}


int open_socket(char* host, int port, int timeout_sec, int keepalive) {
	mbedtls_net_context server_fd;
	mbedtls_net_init(&server_fd);

	char portStr[8];
	sprintf(portStr, "%d", port);

	int ret;
	if ((ret = mbedtls_net_connect(&server_fd, host, portStr, MBEDTLS_NET_PROTO_TCP)) == 0) {
		struct timeval timeout = {.tv_sec = timeout_sec};
		int nodelay = 1;
		lwip_setsockopt(server_fd.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
		lwip_setsockopt(server_fd.fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
		lwip_setsockopt(server_fd.fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
		lwip_setsockopt(server_fd.fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
		return server_fd.fd;
	} else {
        return ret;
	}
}

/*
ssl_client->socket = -1;
ssl_client->socket = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
if (ssl_client->socket < 0) {
	DEBUG_PRINT("ERROR opening socket\r\n");
    return ssl_client->socket;
}

struct sockaddr_in serv_addr;
memset(&serv_addr, 0, sizeof(serv_addr));
serv_addr.sin_family = AF_INET;
serv_addr.sin_addr.s_addr = ipAddress;
serv_addr.sin_port = htons(port);

if (lwip_connect(ssl_client->socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0) {
    timeout = 5000;
    lwip_setsockopt(ssl_client->socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    lwip_setsockopt(ssl_client->socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    lwip_setsockopt(ssl_client->socket, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
    lwip_setsockopt(ssl_client->socket, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable));
    DEBUG_PRINT("Socket ok");
} else {
    printf("\r\nConnect to Server failed!\r\n");
    ret = -1;
    break;
}

fcntl( ssl_client->socket, F_SETFL, fcntl( ssl_client->socket, F_GETFL, 0 ) | O_NONBLOCK );
*/



int start_ssl_client(sslclient_context *ssl_client, unsigned char *rootCABuff, unsigned char *cli_cert, unsigned char *cli_key)
{
    char buf[512];
    int ret, flags;
    size_t free_heap_before = xPortGetFreeHeapSize();
    DEBUG_PRINT("Free heap before TLS %u\n", free_heap_before);

    do {
        DEBUG_PRINT( "Seeding the random number generator\n");
        mbedtls_entropy_init(&ssl_client->entropy_ctx);

        if ((ret = mbedtls_ctr_drbg_seed(&ssl_client->drbg_ctx, mbedtls_entropy_func,
                                         &ssl_client->entropy_ctx, (const unsigned char *) pers, strlen(pers))) != 0) {
            printf( "mbedtls_ctr_drbg_seed returned %d \n", ret);
            break;
        }


        /* MBEDTLS_SSL_VERIFY_REQUIRED if a CA certificate is defined on Arduino IDE and
        MBEDTLS_SSL_VERIFY_NONE if not.
        */
        if (rootCABuff != NULL) {
            DEBUG_PRINT( "Loading CA cert\n");
            mbedtls_x509_crt_init(&ssl_client->ca_cert);
            mbedtls_ssl_conf_authmode(&ssl_client->ssl_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
            ret = mbedtls_x509_crt_parse(&ssl_client->ca_cert, (const unsigned char *)rootCABuff, strlen((const char *)rootCABuff) + 1);
            if (ret < 0) {
				printf( "CA cert: mbedtls_x509_crt_parse returned -0x%x\n\n", -ret);
				break;
			}
            mbedtls_ssl_conf_ca_chain(&ssl_client->ssl_conf, &ssl_client->ca_cert, NULL);
            //mbedtls_ssl_conf_verify(&ssl_client->ssl_ctx, my_verify, NULL );
            ssl_client->has_ca_cert = 1;
        } else {
            mbedtls_ssl_conf_authmode(&ssl_client->ssl_conf, MBEDTLS_SSL_VERIFY_NONE);
        }

        if (cli_cert != NULL && cli_key != NULL) {
            mbedtls_x509_crt_init(&ssl_client->client_cert);
            mbedtls_pk_init(&ssl_client->client_key);

            DEBUG_PRINT( "Loading CRT cert\n");

            ret = mbedtls_x509_crt_parse(&ssl_client->client_cert, (const unsigned char *)cli_cert, strlen((const char *)cli_cert) + 1);

            if (ret < 0) {
                printf( "CRT cert: mbedtls_x509_crt_parse returned -0x%x\n\n", -ret);
                break;
            }
            ssl_client->has_client_cert = 1;

            DEBUG_PRINT( "Loading private key\n");
            ret = mbedtls_pk_parse_key(&ssl_client->client_key, (const unsigned char *)cli_key, strlen((const char *)cli_key) + 1, NULL, 0);

            if (ret < 0) {
                printf( "PRIVATE KEY: mbedtls_x509_crt_parse returned -0x%x\n\n", -ret);
                break;
            }

            ssl_client->has_client_key = 1;

            mbedtls_ssl_conf_own_cert(&ssl_client->ssl_conf, &ssl_client->client_cert, &ssl_client->client_key);
        }

        /*
        // TODO: implement match CN verification

        DEBUG_PRINT( "Setting hostname for TLS session...\n");

                // Hostname set here should match CN in server certificate
        if((ret = mbedtls_ssl_set_hostname(&ssl_client->ssl_ctx, host)) != 0)
        {
            printf( "mbedtls_ssl_set_hostname returned -0x%x\n", -ret);
            break;
        }
        */

        DEBUG_PRINT( "Setting up the SSL/TLS structure...\n");

        if ((ret = mbedtls_ssl_config_defaults(&ssl_client->ssl_conf,
                                               MBEDTLS_SSL_IS_CLIENT,
                                               MBEDTLS_SSL_TRANSPORT_STREAM,
                                               MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
            printf( "mbedtls_ssl_config_defaults returned %d\n", ret);
            break;
        }

        mbedtls_ssl_conf_rng(&ssl_client->ssl_conf, mbedtls_ctr_drbg_random, &ssl_client->drbg_ctx);
#ifdef MBEDTLS_DEBUG_C
        mbedtls_debug_set_threshold(MBEDTLS_DEBUG_LEVEL);
        mbedtls_ssl_conf_dbg(&ssl_client->ssl_conf, mbedtls_debug, NULL);
#endif

        if ((ret = mbedtls_ssl_setup(&ssl_client->ssl_ctx, &ssl_client->ssl_conf)) != 0) {
            printf( "mbedtls_ssl_setup returned -0x%x\n\n", -ret);
            break;
        }

        mbedtls_ssl_set_bio(&ssl_client->ssl_ctx, &ssl_client->socket, mbedtls_net_send, mbedtls_net_recv, NULL );

        DEBUG_PRINT( "Performing the SSL/TLS handshake...\n");

        while ((ret = mbedtls_ssl_handshake(&ssl_client->ssl_ctx)) != 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != -76) {
                printf( "mbedtls_ssl_handshake returned -0x%x\n", -ret);
                break;
            }
            delay(10);
            vPortYield();
        }

        if (cli_cert != NULL && cli_key != NULL) {
            DEBUG_PRINT("Protocol is %s \nCiphersuite is %s\n", mbedtls_ssl_get_version(&ssl_client->ssl_ctx), mbedtls_ssl_get_ciphersuite(&ssl_client->ssl_ctx));
            if ((ret = mbedtls_ssl_get_record_expansion(&ssl_client->ssl_ctx)) >= 0) {
                DEBUG_PRINT("Record expansion is %d\n", ret);
            } else {
                DEBUG_PRINT("Record expansion is unknown (compression)\n");
            }
        }

		DEBUG_PRINT( "Verifying peer X.509 certificate...\n");

		if ((flags = mbedtls_ssl_get_verify_result(&ssl_client->ssl_ctx)) != 0) {
			printf( "Failed to verify peer certificate!\n");
			bzero(buf, sizeof(buf));
			mbedtls_x509_crt_verify_info(buf, sizeof(buf), "  ! ", flags);
			printf( "verification info: %s\n", buf);
			stop_ssl_socket(ssl_client);  //It's not safe continue.
		} else {
			DEBUG_PRINT( "Certificate verified.\n");
		}

    } while (0);

    size_t free_heap_after = xPortGetFreeHeapSize();
    DEBUG_PRINT("Free heap after TLS %u (-%u)\n", free_heap_after, free_heap_before-free_heap_after);

    return ssl_client->socket;
}


void stop_ssl_socket(sslclient_context *ssl_client)
{
    DEBUG_PRINT( "\nCleaning SSL connection.\n");
    close(ssl_client->socket);
    ssl_client->socket = -1;
    mbedtls_ssl_free(&ssl_client->ssl_ctx);
    mbedtls_ssl_config_free(&ssl_client->ssl_conf);
    mbedtls_ctr_drbg_free(&ssl_client->drbg_ctx);
    mbedtls_entropy_free(&ssl_client->entropy_ctx);

    if (ssl_client->has_ca_cert) {
        mbedtls_x509_crt_free(&ssl_client->ca_cert);
        ssl_client->has_ca_cert = 0;
    }

    if (ssl_client->has_client_cert) {
        mbedtls_x509_crt_free(&ssl_client->client_cert);
        ssl_client->has_client_cert = 0;
    }

    if (ssl_client->has_client_key) {
        mbedtls_pk_free(&ssl_client->client_key);
        ssl_client->has_client_key = 0;
    }
}

int data_to_read(sslclient_context *ssl_client)
{

    int ret, res;
    ret = mbedtls_ssl_read(&ssl_client->ssl_ctx, NULL, 0);
    //printf("RET: %i\n",ret);   //for low level debug
    res = mbedtls_ssl_get_bytes_avail(&ssl_client->ssl_ctx);
    //printf("RES: %i\n",res);
    if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret < 0 && ret != -76) {
        printf("MbedTLS error %i", ret);
    }

    return res;
}

int send_ssl_data(sslclient_context *ssl_client, const uint8_t *data, uint16_t len)
{
    //DEBUG_PRINT( "Writing HTTP request...\n");  //for low level debug
    int ret = -1;

    while ((ret = mbedtls_ssl_write(&ssl_client->ssl_ctx, data, len)) <= 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != -76) {
            printf( "mbedtls_ssl_write returned -0x%x\n", -ret);
            break;
        }
    }

    len = ret;
    //DEBUG_PRINT( "%d bytes written\n", len);  //for low level debug
    return ret;
}

int get_ssl_receive(sslclient_context *ssl_client, uint8_t *data, int length)
{
    //DEBUG_PRINT( "Reading HTTP response...\n");   //for low level debug
    int ret = -1;

    ret = mbedtls_ssl_read(&ssl_client->ssl_ctx, data, length);

    //DEBUG_PRINT( "%d bytes readed\n", ret);   //for low level debug
    return ret;
}
