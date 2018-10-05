#ifndef COMPONENTS_BOOTWIFI_INCLUDE_WEB_H_
#define COMPONENTS_BOOTWIFI_INCLUDE_WEB_H_

#undef HTTP_GET
#undef HTTP_POST
#include <http_server.h>

httpd_handle_t start_webserver(void);
void stop_webserver(httpd_handle_t server);
void web_wifi_handler(bool connected, bool ap_mode);

#endif /* COMPONENTS_BOOTWIFI_INCLUDE_WEB_H_ */
