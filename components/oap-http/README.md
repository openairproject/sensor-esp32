# Lightweight HTTP client for ESP32 
## Example 

```cpp
int download_callback(request_t *req, char *data, int len)
{
    req_list_t *found = req->response->header;
    while(found->next != NULL) {
        found = found->next;
        ESP_LOGI(TAG,"Response header %s:%s", (char*)found->key, (char*)found->value);
    }
    //or 
    found = req_list_get_key(req->response->header, "Content-Length");
    if(found) {
        ESP_LOGI(TAG,"Get header %s:%s", (char*)found->key, (char*)found->value);
    }
    ESP_LOGI(TAG,"%s", data);
    return 0;
}
static void request_task(void *pvParameters)
{
    request_t *req;
    int status;
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "Connected to AP, freemem=%d",esp_get_free_heap_size());
    // vTaskDelay(1000/portTICK_RATE_MS);
    req = req_new("http://httpbin.org/post"); 
    //or
    //request *req = req_new("https://google.com"); //for SSL
    req_setopt(req, REQ_SET_METHOD, "POST");
    req_setopt(req, REQ_SET_POSTFIELDS, "test=data&test2=data2");
    req_setopt(req, REQ_FUNC_DOWNLOAD_CB, download_callback);
    status = req_perform(req);
    req_clean(req);
    vTaskDelete(NULL);
}

```

## Usage 
- Create ESP-IDF application https://github.com/espressif/esp-idf-template
- Clone `git submodule add https://github.com/tuanpmt/esp-request components/esp-request`
- Example `esp-request` application: https://github.com/tuanpmt/esp-request-app
- OTA application using `esp-request`: https://github.com/tuanpmt/esp32-fota

## API 

### Function
- `req_new`
- `req_setopt`
- `req_clean`

### Options for `req_setopt`  
- REQ_SET_METHOD - `req_setopt(req, REQ_SET_METHOD, "GET");//or POST/PUT/DELETE`
- REQ_SET_HEADER - `req_setopt(req, REQ_SET_HEADER, "HeaderKey: HeaderValue");`
- REQ_SET_HOST - `req_setopt(req, REQ_SET_HOST, "google.com"); //or 192.168.0.1`
- REQ_SET_PORT - `req_setopt(req, REQ_SET_PORT, "80");//must be string`
- REQ_SET_PATH - `req_setopt(req, REQ_SET_PATH, "/path");`
- REQ_SET_SECURITY
- REQ_SET_URI  - `req_setopt(req, REQ_SET_URI, "http://uri.com"); //will replace host, port, path, security and Auth if present`
- REQ_SET_DATAFIELDS
- REQ_SET_UPLOAD_LEN - Not effect for now
- REQ_FUNC_DOWNLOAD_CB - `req_setopt(req, REQ_FUNC_DOWNLOAD_CB, download_callback);`
- REQ_FUNC_UPLOAD_CB
- REQ_REDIRECT_FOLLOW - `req_setopt(req, REQ_REDIRECT_FOLLOW, "true"); //or "false"`

### URI format 
- Follow this: https://en.wikipedia.org/wiki/Uniform_Resource_Identifier

## Todo  
- [x] Support URI parser
- [x] Follow redirect
- [x] Support SSL
- [x] Support Set POST Fields (simple)
- [ ] Support Basic Auth
- [ ] Support Upload multipart
- [ ] Support Cookie

## Known Issues 
- ~~Memory leak~~
- Uri parse need more work

## Authors
- [Tuan PM](https://twitter.com/tuanpmt)
