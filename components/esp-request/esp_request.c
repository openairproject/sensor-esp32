/*
* @2017
* Tuan PM <tuanpm at live dot com>
*/

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "esp_request.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/igmp.h"
#include "req_list.h"

#include "mbedtls/platform.h"
#include "mbedtls/net.h"
#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/ssl_internal.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "oap_common.h"

#define TAG "http"

#define SSL_MUTEX 1

static const char *pers = "esp32-tls";

#define REQ_BUFFER_LEN (2048)
#define REQ_CHECK(check, log, ret) if(check) {ESP_LOGE(TAG, log);ret;}

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
        printf( "%s:%d %s", file, line, str);
        break;
    case 2:
    case 3:
        printf( "%s:%d %s", file, line, str);
        break;
    case 4:
        printf( "%s:%d %s", file, line, str);
        break;
    default:
        printf( "Unexpected log level %d: %s", level, str);
        break;
    }
}

#endif


static int resolve_dns(const char *host, struct sockaddr_in *ip) {
    struct hostent *he;
    struct in_addr **addr_list;
    he = gethostbyname(host);
    if(he == NULL)
        return -1;
    addr_list = (struct in_addr **)he->h_addr_list;
    if(addr_list[0] == NULL)
        return -1;
    ip->sin_family = AF_INET;
    memcpy(&ip->sin_addr, addr_list[0], sizeof(ip->sin_addr));
    return 0;
}

static char *http_auth_basic_encode(const char *username, const char *password)
{
    return NULL;
}


static int nossl_connect(request_t *req)
{
    int socket;
    struct sockaddr_in remote_ip;
    struct timeval tv;
    req_list_t *host, *port, *timeout;
    bzero(&remote_ip, sizeof(struct sockaddr_in));
    //if stream_host is not ip address, resolve it AF_INET,servername,&serveraddr.sin_addr
    host = req_list_get_key(req->opt, "host");
    REQ_CHECK(host == NULL, "host = NULL", return -1);

    if(inet_pton(AF_INET, (const char*)host->value, &remote_ip.sin_addr) != 1) {
        if(resolve_dns((const char*)host->value, &remote_ip) < 0) {
            return -1;
        }
    }

    socket = socket(PF_INET, SOCK_STREAM, 0);
    REQ_CHECK(socket < 0, "socket failed", return -1);

    port = req_list_get_key(req->opt, "port");
    if(port == NULL)
        return -1;

    remote_ip.sin_family = AF_INET;
    remote_ip.sin_port = htons(atoi(port->value));

    tv.tv_sec = 10; //default timeout is 10 seconds
    timeout = req_list_get_key(req->opt, "timeout");
    if(timeout) {
        tv.tv_sec = atoi(timeout->value);
    }
    tv.tv_usec = 0;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGD(TAG, "[sock=%d],connecting to server IP:%s,Port:%s...",
             socket, ipaddr_ntoa((const ip_addr_t*)&remote_ip.sin_addr.s_addr), (char*)port->value);
    if(connect(socket, (struct sockaddr *)(&remote_ip), sizeof(struct sockaddr)) != 0) {
        close(socket);
        return -1;
    }
    req->socket = socket;
    return socket;
}

void req_free_x509_crt(mbedtls_x509_crt* crt) {
	mbedtls_x509_crt_free(crt);
	free(crt);
}

mbedtls_x509_crt* req_parse_x509_crt(unsigned char *buf, size_t buflen) {
	mbedtls_x509_crt* crt = malloc(sizeof(mbedtls_x509_crt));
	mbedtls_x509_crt_init(crt);
	esp_err_t ret = mbedtls_x509_crt_parse(crt, buf, buflen);
	if (ret == ESP_OK) {
		return crt;
	} else {
		ESP_LOGW(TAG, "failed to parse x509 cert: 0x%x", ret);
		req_free_x509_crt(crt);
		return NULL;
	}
}

void req_free_pkey(mbedtls_pk_context* pkey) {
	mbedtls_pk_free(pkey);
	free(pkey);
}

mbedtls_pk_context* req_parse_pkey(unsigned char* buf, size_t buflen) {
	mbedtls_pk_context* pkey = malloc(sizeof(mbedtls_pk_context));
	mbedtls_pk_init(pkey);
	esp_err_t ret = mbedtls_pk_parse_key(pkey, buf, buflen, NULL, 0);
	if (ret == ESP_OK) {
		return pkey;
	} else {
		ESP_LOGW(TAG, "failed to parse key: 0x%x", -ret);
		req_free_pkey(pkey);
		return NULL;
	}
}


/*
 * making concurrent SSL requests is problematic - they consume a lot of heap,
 * and there's something wrong with mbedtls internally. we create a mutex to allow only one at once.
 */
#if SSL_MUTEX
	static SemaphoreHandle_t ssl_semaphore = NULL;
#endif

static int mbedtls_connect(request_t *req)
{
#if SSL_MUTEX
	if (!ssl_semaphore) ssl_semaphore = xSemaphoreCreateMutex();
	if(!xSemaphoreTake( ssl_semaphore, ( TickType_t ) 10000 / portTICK_PERIOD_MS )) {
		ESP_LOGW(TAG, "couldn't acquire lock for SSL request");
		return ESP_FAIL;
	}
#endif

    nossl_connect(req);
    REQ_CHECK(req->socket < 0, "socket failed", return -1);
    int ret;

    req_ssl* ssl = malloc(sizeof(req_ssl));
    if (!ssl) return ESP_FAIL;
    memset(ssl, 0, sizeof(req_ssl));
    req->ssl = ssl;
	mbedtls_ssl_init(&ssl->ssl_ctx);
	mbedtls_ssl_config_init(&ssl->ssl_conf);
	mbedtls_ctr_drbg_init(&ssl->drbg_ctx);

    do {
    	ESP_LOGV(TAG, "Seeding the random number generator");
        mbedtls_entropy_init(&ssl->entropy_ctx);

        if ((ret = mbedtls_ctr_drbg_seed(&ssl->drbg_ctx, mbedtls_entropy_func,
                                         &ssl->entropy_ctx, (const unsigned char *) pers, strlen(pers))) != ESP_OK) {
        	ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed returned %d", -ret);
            break;
        }


        /* MBEDTLS_SSL_VERIFY_REQUIRED if a CA certificate is defined on Arduino IDE and
        MBEDTLS_SSL_VERIFY_NONE if not.
        */
        if (req->ca_cert) {
        	ESP_LOGD(TAG, "Set CA certificate");
            mbedtls_ssl_conf_authmode(&ssl->ssl_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
            mbedtls_ssl_conf_ca_chain(&ssl->ssl_conf, req->ca_cert, NULL);
            //mbedtls_ssl_conf_verify(&ssl->ssl_ctx, my_verify, NULL );
        } else {
            mbedtls_ssl_conf_authmode(&ssl->ssl_conf, MBEDTLS_SSL_VERIFY_NONE);
        }

        if (req->client_cert && req->client_key) {
            ESP_LOGD(TAG, "Set client cert/pkey");
            if ((ret = mbedtls_ssl_conf_own_cert(&ssl->ssl_conf, req->client_cert, req->client_key)) != ESP_OK) {
                ESP_LOGW(TAG, "mbedtls_ssl_conf_own_cert returned 0x%x", -ret);
                break;
            }
        }

        // Hostname set here should match CN in server certificate
        if((ret = mbedtls_ssl_set_hostname(&ssl->ssl_ctx, req_list_get_key(req->opt, "host")->value)) != ESP_OK)
        {
            ESP_LOGW(TAG, "mbedtls_ssl_set_hostname returned 0x%x", -ret);
            break;
        }

        ESP_LOGD(TAG, "Setting up the SSL/TLS structure...");

        if ((ret = mbedtls_ssl_config_defaults(&ssl->ssl_conf,
                                               MBEDTLS_SSL_IS_CLIENT,
                                               MBEDTLS_SSL_TRANSPORT_STREAM,
                                               MBEDTLS_SSL_PRESET_DEFAULT)) != ESP_OK) {
        	ESP_LOGW(TAG, "mbedtls_ssl_config_defaults returned 0x%x", -ret);
            break;
        }

        mbedtls_ssl_conf_rng(&ssl->ssl_conf, mbedtls_ctr_drbg_random, &ssl->drbg_ctx);
#ifdef MBEDTLS_DEBUG_C
        mbedtls_debug_set_threshold(MBEDTLS_DEBUG_LEVEL);
        mbedtls_ssl_conf_dbg(&ssl->ssl_conf, mbedtls_debug, NULL);
#endif

        if ((ret = mbedtls_ssl_setup(&ssl->ssl_ctx, &ssl->ssl_conf)) != ESP_OK) {
        	ESP_LOGW(TAG, "mbedtls_ssl_setup returned 0x%x", -ret);
            break;
        }

        mbedtls_ssl_set_bio(&ssl->ssl_ctx, &req->socket, mbedtls_net_send, mbedtls_net_recv, NULL );

        ESP_LOGD(TAG, "Performing the SSL/TLS handshake...");

        int attempts = 0;
        while ((ret = mbedtls_ssl_handshake(&ssl->ssl_ctx)) != ESP_OK && attempts < 10) {
        	//careful here!
        	// ret = 0x4c happens from time to time, esp just after network init - sometimes it recovers, sometimes it doesn't
        	// it cannot recover from 0x4e (no network)
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != 0x4c) {
            	ESP_LOGW(TAG, "mbedtls_ssl_handshake returned 0x%x", -ret);
                break;
            }
            attempts++;
            ESP_LOGD(TAG, "handshake failed (0x%x), try again (%d)", -ret, attempts);
            vTaskDelay(10 / portTICK_PERIOD_MS);
            vPortYield();
        }
        if (ret != ESP_OK) {
        	//do we need to release anything here???
        	break;
        }

        if (req->client_cert && req->client_key) {
            if ((ret = mbedtls_ssl_get_record_expansion(&ssl->ssl_ctx)) >= 0) {
            	ESP_LOGD(TAG, "Record expansion is %d\n", ret);
            } else {
                ESP_LOGD(TAG, "Record expansion is unknown (compression)"); //not error
            }
        }

		ESP_LOGD(TAG, "Verifying peer X.509 certificate...");

		if ((ret = mbedtls_ssl_get_verify_result(&ssl->ssl_ctx)) != ESP_OK) {
			ESP_LOGE(TAG, "Failed to verify peer certificate!");
			char buf[512];
			bzero(buf, sizeof(buf));
			mbedtls_x509_crt_verify_info(buf, sizeof(buf), "  ! ", ret);
			ESP_LOGW(TAG, "verification info: %s", buf);
			break;
		} else {
			ESP_LOGD(TAG, "Certificate verified.");
		}

    } while (0);
	return ret;
}

static int mbedtls_write(request_t *req, char *buffer, int len)
{
	//fwrite(buffer, len, 1, stdout);
    int ret = -1;
    while ((ret = mbedtls_ssl_write(&req->ssl->ssl_ctx, (unsigned char *)buffer, len)) <= 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != -76) {
        	ESP_LOGE(TAG, "mbedtls_ssl_write returned 0x%x", ret);
            break;
        }
    }
    return ret;
}

static int nossl_write(request_t *req, char *buffer, int len)
{
    return write(req->socket, buffer, len);
}

static int mbedtls_read(request_t *req, char *buffer, int len)
{
	int ret;
	do {
		ret = mbedtls_ssl_read(&req->ssl->ssl_ctx, (unsigned char *)buffer, len);
		if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
			continue;
		} else if (ret == -0x4C) {
			ESP_LOGW(TAG, "timeout");
			break;
		} else if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
			ESP_LOGW(TAG, "peer close");
			break;
		} else {
			break;
		}
	} while (1);
	return ret;
}

static int nossl_read(request_t *req, char *buffer, int len)
{
    return read(req->socket, buffer, len);
}

static int mbedtls_close(request_t *req)
{
	ESP_LOGD(TAG, "Cleaning SSL connection.");
	close(req->socket);

	if (req->ssl) {
		mbedtls_ssl_free(&req->ssl->ssl_ctx);
		mbedtls_ssl_config_free(&req->ssl->ssl_conf);
		mbedtls_ctr_drbg_free(&req->ssl->drbg_ctx);
		mbedtls_entropy_free(&req->ssl->entropy_ctx);
		free(req->ssl);
		req->ssl = NULL;
	}

#if SSL_MUTEX
	xSemaphoreGive(ssl_semaphore);
#endif

	return 0;
}

static int nossl_close(request_t *req)
{
    return close(req->socket);
}
static int req_setopt_from_uri(request_t *req, const char* uri)
{
    //TODO: relative path
    parsed_uri_t *puri;
    char port[] = "443";
    puri = parse_uri(uri);
    REQ_CHECK(puri == NULL, "Error parse uri", return -1);

    if(strcasecmp(puri->scheme, "https") == 0) {
        req_setopt(req, REQ_SET_SECURITY, "true");
    } else {
        req_setopt(req, REQ_SET_SECURITY, "false");
        strcpy(port, "80");
        port[2] = 0;
    }

    if(puri->username && puri->password) {
        char *auth = http_auth_basic_encode(puri->username, puri->password);
        if(auth) {
            req_setopt(req, REQ_SET_HEADER, auth);
            free(auth);
        }

    }

    req_setopt(req, REQ_SET_HOST, puri->host);
    req_setopt(req, REQ_SET_PATH, puri->path);
    //port
    if(puri->port) {
        req_setopt(req, REQ_SET_PORT, puri->port);
    } else {
        req_setopt(req, REQ_SET_PORT, port);
    }
    free_parsed_uri(puri);
    return 0;
}
request_t *req_new_with_buf(const char *uri, size_t buffer_size)
{
    request_t *req = malloc(sizeof(request_t));

    REQ_CHECK(req == NULL, "Error allocate req", return NULL);
    memset(req, 0, sizeof(request_t));

    req->buffer = malloc(sizeof(req_buffer_t));
    req->buffer_size = buffer_size;
    REQ_CHECK(req->buffer == NULL, "Error allocate buffer", return NULL);
    memset(req->buffer, 0, sizeof(req_buffer_t));

    req->buffer->data = malloc(req->buffer_size + 1); //1 byte null for end of string
    //TODO: Free req before return
    REQ_CHECK(req->buffer->data == NULL, "Error allocate buffer", return NULL);

    req->opt = malloc(sizeof(req_list_t));
    memset(req->opt, 0, sizeof(req_list_t));
    req->header = malloc(sizeof(req_list_t));
    memset(req->header, 0, sizeof(req_list_t));

    req->response = malloc(sizeof(response_t));
    REQ_CHECK(req->response == NULL, "Error create response", return NULL);
    memset(req->response, 0, sizeof(response_t));

    req->response->header = malloc(sizeof(req_list_t));
    REQ_CHECK(req->response->header == NULL, "Error create response header", return NULL);
    memset(req->response->header, 0, sizeof(req_list_t));

    req->ssl = NULL;
    req->ca_cert = NULL;
    req->client_cert = NULL;
    req->client_key = NULL;
    req->meta = NULL;

    req_setopt_from_uri(req, uri);

    req_setopt(req, REQ_REDIRECT_FOLLOW, "true");
    req_setopt(req, REQ_SET_METHOD, "GET");
    req_setopt(req, REQ_SET_HEADER, "User-Agent: ESP32 Http Client");
    return req;
}


request_t *req_new(const char *uri) {
	return req_new_with_buf(uri, REQ_BUFFER_LEN);
}

void req_setopt(request_t *req, REQ_OPTS opt, void* data)
{
    int post_len;
    char len_str[10] = {0};
    req_list_t *tmp;
    char *host_w_port = malloc(1024);
    if(!req || !data)
        return;
    switch(opt) {
        case REQ_SET_METHOD:
            req_list_set_key(req->opt, "method", data);
            break;
        case REQ_SET_HEADER:
            req_list_set_from_string(req->header, data);
            break;
        case REQ_SET_HOST:
            req_list_set_key(req->opt, "host", data);
            tmp = req_list_get_key(req->opt, "port");
            if(tmp != NULL) {
                sprintf(host_w_port, "%s:%s", (char*)data, (char*)tmp->value);
            } else {
                sprintf(host_w_port, "%s", (char*)data);
            }
            req_list_set_key(req->header, "Host", host_w_port);
            break;
        case REQ_SET_PORT:
            req_list_set_key(req->opt, "port", data);
            tmp = req_list_get_key(req->opt, "host");
            if(tmp != NULL) {
                sprintf(host_w_port, "%s:%s", (char*)tmp->value, (char*)data);
                req_list_set_key(req->header, "Host", host_w_port);
            }

            break;
        case REQ_SET_PATH:
            req_list_set_key(req->opt, "path", data);
            break;
        case REQ_SET_URI:
            req_setopt_from_uri(req, data);
            break;
        case REQ_SET_SECURITY:
            req_list_set_key(req->opt, "secure", data);
            if(req_list_check_key(req->opt, "secure", "true")) {
                ESP_LOGD(TAG, "Secure");
                req->_read = mbedtls_read;
                req->_write = mbedtls_write;
                req->_connect = mbedtls_connect;
                req->_close = mbedtls_close;
            } else {
                req->_read = nossl_read;
                req->_write = nossl_write;
                req->_connect = nossl_connect;
                req->_close = nossl_close;
            }

            break;
        case REQ_SET_POSTFIELDS:
            req_list_set_key(req->header, "Content-Type", "application/x-www-form-urlencoded");
            req_list_set_key(req->opt, "method", HTTP_POST);
        case REQ_SET_DATAFIELDS:
            post_len = strlen((char*)data);
            sprintf(len_str, "%d", post_len);
            req_list_set_key(req->opt, "postfield", data);
            req_list_set_key(req->header, "Content-Length", len_str);
            break;
        case REQ_FUNC_UPLOAD_CB:
            req->upload_callback = data;
            break;
        case REQ_FUNC_DOWNLOAD_CB:
            req->download_callback = data;
            break;
        case REQ_REDIRECT_FOLLOW:
            req_list_set_key(req->opt, "follow", data);
            break;
        default:
            break;
    }
    free(host_w_port);
}
static int req_process_upload(request_t *req)
{
    int tx_write_len = 0;
    req_list_t *found;


    found = req_list_get_key(req->opt, "method");
    REQ_CHECK(found == NULL, "method required", return -1);
    tx_write_len += sprintf(req->buffer->data + tx_write_len, "%s ", (char*)found->value);

    found = req_list_get_key(req->opt, "path");
    REQ_CHECK(found == NULL, "path required", return -1);
    tx_write_len += sprintf(req->buffer->data + tx_write_len, "%s HTTP/1.1\r\n", (char*)found->value);

    //TODO: Check header len < REQ_BUFFER_LEN
    found = req->header;
    while(found->next != NULL) {
        found = found->next;
        tx_write_len += sprintf(req->buffer->data + tx_write_len, "%s: %s\r\n", (char*)found->key, (char*)found->value);
    }
    tx_write_len += sprintf(req->buffer->data + tx_write_len, "\r\n");

    // ESP_LOGD(TAG, "Request header, len= %d, real_len= %d\r\n%s", tx_write_len, strlen(req->buffer->data), req->buffer->data);

    REQ_CHECK(req->_write(req, req->buffer->data, tx_write_len) < 0, "Error write header", return -1);

    found = req_list_get_key(req->opt, "postfield");
    if(found) {
        ESP_LOGV(TAG, "begin write %d bytes", strlen((char*)found->value));
        int bwrite = req->_write(req, (char*)found->value, strlen((char*)found->value));
        ESP_LOGV(TAG, "end write %d bytes", bwrite);
        if(bwrite < 0) {
            ESP_LOGE(TAG, "Error write");
            return -1;
        }
    }

    if(req->upload_callback) {
        while((tx_write_len = req->upload_callback(req, (void *)req->buffer->data, req->buffer_size)) > 0) {
            REQ_CHECK(req->_write(req, req->buffer->data, tx_write_len) < 0, "Error write data", return -1);
        }
    }
    return 0;
}

static int reset_buffer(request_t *req)
{
    req->buffer->bytes_read = 0;
    req->buffer->bytes_write = 0;
    req->buffer->at_eof = 0;
    req->buffer->bytes_total = 0;
    return 0;
}

static int fill_buffer(request_t *req)
{
    int bread;
    int bytes_inside_buffer = req->buffer->bytes_write - req->buffer->bytes_read;
    int buffer_free_bytes;
    if(bytes_inside_buffer)
    {
        memmove((void*)req->buffer->data, (void*)(req->buffer->data + req->buffer->bytes_read),
                bytes_inside_buffer);
        req->buffer->bytes_read = 0;
        req->buffer->bytes_write = bytes_inside_buffer;
        if(req->buffer->bytes_write < 0)
            req->buffer->bytes_write = 0;
        ESP_LOGV(TAG, "move=%d, write=%d, read=%d", bytes_inside_buffer, req->buffer->bytes_write, req->buffer->bytes_read);
    }
    if(!req->buffer->at_eof)
    {
        //reset if buffer full
        if(req->buffer->bytes_write == req->buffer->bytes_read) {
            req->buffer->bytes_write = 0;
            req->buffer->bytes_read = 0;
        }
        buffer_free_bytes = req->buffer_size - req->buffer->bytes_write;
        ESP_LOGV(TAG, "Begin read %d bytes", buffer_free_bytes);
        bread = req->_read(req, (void*)(req->buffer->data + req->buffer->bytes_write), buffer_free_bytes);
        // ESP_LOGD(TAG, "bread = %d, bytes_write = %d, buffer_free_bytes = %d", bread, req->buffer->bytes_write, buffer_free_bytes);
        ESP_LOGV(TAG, "End read, byte read= %d bytes", bread);
        if(bread < 0) {
            req->buffer->at_eof = 1;
            return -1;
        }
        req->buffer->bytes_write += bread;
        req->buffer->data[req->buffer->bytes_write] = 0;//terminal string

        if(bread == 0) {
            req->buffer->at_eof = 1;
        }
    }

    return 0;
}


static char *req_readline(request_t *req)
{
    char *cr, *ret = NULL;
    if(req->buffer->bytes_read + 2 > req->buffer->bytes_write) {
        return NULL;
    }
    cr = strstr(req->buffer->data + req->buffer->bytes_read, "\r\n");
    if(cr == NULL) {
        return NULL;
    }
    memset(cr, 0, 2);
    ret = req->buffer->data + req->buffer->bytes_read;
    req->buffer->bytes_read += (cr - (req->buffer->data + req->buffer->bytes_read)) + 2;
    // ESP_LOGD(TAG, "next offset=%d", req->buffer->bytes_read);
    return ret;
}
static int req_process_download(request_t *req)
{
    int process_header = 1, header_off = 0;
    char *line;
    req_list_t *content_len;
    req->response->status_code = -1;
    reset_buffer(req);
    req_list_clear(req->response->header);
    req->response->len = 0;
    do {
        fill_buffer(req);
        if(process_header) {
            while((line = req_readline(req)) != NULL) {
                if(line[0] == 0) {
                    ESP_LOGV(TAG, "end process_idx=%d", req->buffer->bytes_read);
                    header_off = req->buffer->bytes_read;
                    process_header = 0; //end of http header
                    break;
                } else {
                    if(req->response->status_code < 0) {
                        char *temp = strstr(line, "HTTP/1.");
                        if(temp) {
                            char statusCode[4] = { 0 };
                            memcpy(statusCode, temp + 9, 3);
                            req->response->status_code = atoi(statusCode);
                            ESP_LOGD(TAG, "status code: %d", req->response->status_code);
                        }
                    } else {
                        req_list_set_from_string(req->response->header, line);
                        ESP_LOGV(TAG, "header line: %s", line);
                    }
                }
            }
        }

        if(process_header == 0)
        {
            if(req->buffer->at_eof) {
                fill_buffer(req);
            }

            req->buffer->bytes_read = req->buffer->bytes_write;
            content_len = req_list_get_key(req->response->header, "Content-Length");
            if(content_len) {
                req->response->len = atoi(content_len->value);
            }
            if(req->response->len && req->download_callback && (req->buffer->bytes_write - header_off) != 0) {
                if(req->download_callback(req, (void *)(req->buffer->data + header_off), req->buffer->bytes_write - header_off) < 0) break;

                req->buffer->bytes_total += req->buffer->bytes_write - header_off;
                if(req->buffer->bytes_total == req->response->len) {
                    break;
                }
            }
            header_off = 0;
            if(req->response->len == 0) {
                break;
            }

        }

    } while(req->buffer->at_eof == 0);
    return 0;
}

int req_perform(request_t *req)
{
    do {
    	ESP_LOGD(TAG, "%s %s%s",
    			(char*)req_list_get_key(req->opt, "method")->value,
				(char*)req_list_get_key(req->opt, "host")->value,
				(char*)req_list_get_key(req->opt, "path")->value);
        REQ_CHECK(req->_connect(req) < 0, "Error connnect", break);
        REQ_CHECK(req_process_upload(req) < 0, "Error send request", break);
        REQ_CHECK(req_process_download(req) < 0, "Error download", break);

        if((req->response->status_code == 301 || req->response->status_code == 302) && req_list_check_key(req->opt, "follow", "true")) {
            req_list_t *found = req_list_get_key(req->response->header, "Location");
            if(found) {
                req_list_set_key(req->header, "Referer", (const char*)found->value);
                req_setopt_from_uri(req, (const char*)found->value);
                ESP_LOGD(TAG, "Following: %s", (char*)found->value);
                req->_close(req);
                continue;
            }
            break;
        } else {
            break;
        }
    } while(1);
    req->_close(req);
    return req->response->status_code;
}

void req_clean(request_t *req)
{
    req_list_clear(req->opt);
    req_list_clear(req->header);
    req_list_clear(req->response->header);
    free(req->opt);
    free(req->header);
    free(req->response->header);
    free(req->response);
    free(req->buffer->data);
    free(req->buffer);

    if (req->ca_cert) {
    	req_free_x509_crt(req->ca_cert);
	}
	if (req->client_cert) {
		req_free_x509_crt(req->client_cert);
	}
	if (req->client_key) {
		req_free_pkey(req->client_key);
	}

    free(req);
}
