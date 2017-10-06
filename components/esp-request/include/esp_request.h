#ifndef _ESP_REQUEST_H_
#define _ESP_REQUEST_H_
#include "req_list.h"
#include "uri_parser.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

#define HTTP_HEADER_CONTENT_TYPE_JSON "Content-Type: application/json"
#define HTTP_HEADER_CONNECTION_CLOSE "Connection: close"
#define HTTP_POST 	"POST"
#define HTTP_GET 	"GET"

typedef enum {
    REQ_SET_METHOD = 0x01,
    REQ_SET_HEADER,
    REQ_SET_HOST,
    REQ_SET_PORT,
    REQ_SET_PATH,
    REQ_SET_URI,
    REQ_SET_SECURITY,
    REQ_SET_POSTFIELDS,
    REQ_SET_DATAFIELDS,
    REQ_SET_UPLOAD_LEN,
    REQ_FUNC_DOWNLOAD_CB,
    REQ_FUNC_UPLOAD_CB,
    REQ_REDIRECT_FOLLOW
} REQ_OPTS;

typedef struct response_t {
    req_list_t *header;
    int status_code;
    int len;
} response_t;

typedef struct {
	size_t buffer_length;
	int bytes_read;
    int bytes_write;
    int bytes_total;
    char *data;
    int at_eof;
} req_buffer_t;


typedef struct {
    mbedtls_ssl_context ssl_ctx;
    mbedtls_ssl_config ssl_conf;
    mbedtls_ctr_drbg_context drbg_ctx;
    mbedtls_entropy_context entropy_ctx;
} req_ssl;

typedef struct request_t {
    req_list_t *opt;
    req_list_t *header;

    req_buffer_t *buffer;
    size_t buffer_size;
    void *context;
    int socket;
    int (*_connect)(struct request_t *req);
    int (*_read)(struct request_t *req, char *buffer, int len);
    int (*_write)(struct request_t *req, char *buffer, int len);
    int (*_close)(struct request_t *req);
    int (*upload_callback)(struct request_t *req, void *buffer, int len);
    int (*download_callback)(struct request_t *req, void *buffer, int len);
    response_t *response;

    //mbedtls
    req_ssl* ssl;
    mbedtls_x509_crt* ca_cert;
    mbedtls_x509_crt* client_cert;
    mbedtls_pk_context* client_key;

    //meta
    void* meta;

} request_t;

typedef int (*download_cb)(request_t *req, void *buffer, int len);
typedef int (*upload_cb)(request_t *req, void *buffer, int len);


request_t *req_new(const char *url);
request_t *req_new_with_buf(const char *uri, size_t buffer_size);
void req_setopt(request_t *req, REQ_OPTS opt, void* data);
void req_clean(request_t *req);
int req_perform(request_t *req);

void req_free_x509_crt(mbedtls_x509_crt* crt);
void req_free_pkey(mbedtls_pk_context* pkey);
mbedtls_x509_crt* req_parse_x509_crt(unsigned char *buf, size_t buflen);
mbedtls_pk_context* req_parse_pkey(unsigned char* buf, size_t buflen);

#endif
