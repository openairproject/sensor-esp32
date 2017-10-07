/*
 * test_esp_request.c
 *
 *  Created on: Oct 6, 2017
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


#include "esp_request.h"
#include "oap_test.h"
#include "oap_debug.h"

extern const uint8_t _root_ca_pem_start[] asm("_binary_comodo_ca_pem_start");
extern const uint8_t _root_ca_pem_end[]   asm("_binary_comodo_ca_pem_end");

#define URL(PROT,PATH) #PROT"://openairproject.com/ota-test"#PATH

static int make_http_get() {
	request_t* req = req_new(URL(http,/index.txt));
	if (!req) return -1;
	req_setopt(req, REQ_REDIRECT_FOLLOW, "false");
	req_setopt(req, REQ_SET_HEADER, HTTP_HEADER_CONNECTION_CLOSE);
	int resp = req_perform(req);
	req_clean(req);
	return resp == 301 ? 0 : resp;
}

static int make_https_get(int big) {
	request_t* req = req_new(big ? URL(https,/random32k.bin) : URL(https,/index.txt));
	if (!req) return -1;
    req->ca_cert = req_parse_x509_crt((unsigned char*)_root_ca_pem_start, _root_ca_pem_end-_root_ca_pem_start);
	req_setopt(req, REQ_SET_HEADER, HTTP_HEADER_CONNECTION_CLOSE);
	int resp = req_perform(req);
	req_clean(req);
	return resp == 200 ? 0 : resp;
}

TEST_CASE("http request", "[esp-request]") {
	test_require_wifi();
	TEST_ESP_OK(make_http_get());
}

TEST_CASE("https request", "[esp-request]") {
	test_require_wifi();
	TEST_ESP_OK(make_https_get(0));
}

/*
 * there's a few problems with SSL stack on ESP32.
 * a single SSL request consumes a lot of stack and heap.
 *
 * so I've implemented a mutex to process them sequentially.
 * even with queuing them, they are still quite unreliable and timeout often.
 *
 * these tests are handy to look for memory leaks and stability in general.
 */
static QueueHandle_t https_client_task_queue;

static void https_client_task() {
	int resp = make_https_get(1);
	xQueueSend(https_client_task_queue, &resp, 100);
	log_task_stack("sslclient");
	vTaskDelete(NULL);
}

TEST_CASE("multiple ssl requests", "[esp-request]") {
	test_require_wifi();
	int count = 4;
	if (!https_client_task_queue) https_client_task_queue = xQueueCreate(count, sizeof(int));
	log_heap_size("before https requests");
	for (int i = 0; i < count; i++) {
		char t[20]; //should be malloc ?
		sprintf(t, "https_task_%i", i);
		//6KB is MINIMUM for making ssl!
		xTaskCreate(https_client_task, t, 1024*6, NULL, 10, NULL);
	}
	int resp;
	for (int i = 0; i < count; i++) {
		xQueueReceive(https_client_task_queue, &resp, 20000/portTICK_PERIOD_MS);
	}
	log_heap_size("after http requests");
}


static QueueHandle_t http_client_task_queue;

static void http_client_task() {
	int resp = make_http_get();
	xQueueSend(http_client_task_queue, &resp, 100);
	log_task_stack("httpclient");
	vTaskDelete(NULL);
}

//no problems here, even if sometimes one or two fails
TEST_CASE("multiple http requests", "[esp-request]") {
	test_require_wifi();
	int count = 10;
	if (!http_client_task_queue) http_client_task_queue = xQueueCreate(count, sizeof(int));
	log_heap_size("before http requests");
	for (int i = 0; i < count; i++) {
		char t[20];
		sprintf(t, "http_task_%i", i);
		xTaskCreate(http_client_task, t, 1024*5, NULL, 10, NULL);
	}
	int resp;
	for (int i = 0; i < count; i++) {
		xQueueReceive(http_client_task_queue, &resp, 20000/portTICK_PERIOD_MS);
	}
	log_heap_size("after http requests");
}


