#include "udp_server.h"
#include "hw_gpio.h"

#ifdef CONFIG_OAP_GPIO_UDP_ENABLED

#define PORT_NUMBER 6789
#define BUFLEN 512

#ifdef XML_LARGE_SIZE
# if defined(XML_USE_MSC_EXTENSIONS) && _MSC_VER < 1400
#  define XML_FMT_INT_MOD "I64"
# else
#  define XML_FMT_INT_MOD "ll"
# endif
#else
# define XML_FMT_INT_MOD "l"
#endif

#ifdef XML_UNICODE_WCHAR_T
# define XML_FMT_STR "ls"
#else
# define XML_FMT_STR "s"
#endif

static char tag[]="udpserver";
int mysocket;
typedef struct {
    int depth;
    int gpio;
    int value;
    int delay;
} gpio_vals_t;

int get_socket_error_code(int socket)
{
    int result;
    u32_t optlen = sizeof(int);
    if(getsockopt(socket, SOL_SOCKET, SO_ERROR, &result, &optlen) == -1) {
        ESP_LOGE(tag, "getsockopt failed");
        return -1;
    }
    return result;
    
}

int show_socket_error_reason(int socket)
{
    int err = get_socket_error_code(socket);
    ESP_LOGW(tag, "socket error %d %s", err, strerror(err));
    return err;
}

void close_socket(int socket)
{
    close(socket);
}

static void XMLCALL startElement(void *userData, const XML_Char *name, const XML_Char **attr)
{
    gpio_vals_t *gpiovals = (gpio_vals_t *)userData;
    
    if(!strcasecmp(name, "gpio")) {
        int valid=0;
        for (int i = 0; attr[i]; i += 2) {
            const char *an=attr[i];
            const char *av=attr[i+1];
            if(!strcasecmp(an, "num")) {
                gpiovals->gpio=atoi(av);
                valid++;
            }else if(!strcasecmp(an, "delay")) {
                gpiovals->delay=atoi(av);
                valid++;
            }else if(!strcasecmp(an, "value")) {
                gpiovals->value=atoi(av);
                valid++;
            }
        }
        if(valid==3) {
            hw_gpio_queue_trigger(gpiovals->gpio, gpiovals->value, gpiovals->delay);
        }
    }
//    ESP_LOGI(tag, "gpio: %d delay: %d value: %d", gpiovals->gpio, gpiovals->delay, gpiovals->value);
    gpiovals->depth++;
}

static void XMLCALL endElement(void *userData, const XML_Char *name)
{
    gpio_vals_t *gpiovals = (gpio_vals_t *)userData;
    gpiovals->depth--;
}


// UDP Listener
esp_err_t udp_server()
{
    struct sockaddr_in si_other;
    
    unsigned int slen = sizeof(si_other),recv_len;
    char buf[BUFLEN];
    
    // bind to socket
    ESP_LOGI(tag, "bind_udp_server port:%d", PORT_NUMBER);
    mysocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (mysocket < 0) {
        show_socket_error_reason(mysocket);
        return ESP_FAIL;
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT_NUMBER);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(mysocket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        show_socket_error_reason(mysocket);
        close(mysocket);
        return ESP_FAIL;
    } else {
        ESP_LOGI(tag,"socket created without errors");
        while(1) {
//c           ESP_LOGI(tag,"Waiting for incoming data");
            memset(buf,0,BUFLEN);
            
            if ((recv_len = recvfrom(mysocket, buf, BUFLEN, 0, (struct sockaddr *) &si_other, &slen)) == -1) {
                ESP_LOGE(tag,"recvfrom");
            }
            
//            ESP_LOGI(tag,"Received packet from %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
//            ESP_LOGI(tag,"Data: %s -- %d\n" , buf, recv_len);
            
            if ((recv_len + 1) < BUFLEN) {
                buf[recv_len + 1] = '\0';
                XML_Parser parser = XML_ParserCreate(NULL);
                gpio_vals_t gpiovals;
                XML_SetUserData(parser, &gpiovals);
                XML_SetElementHandler(parser, startElement, endElement);
                if (XML_Parse(parser, buf, (int)recv_len, 1) == XML_STATUS_ERROR) {
                    ESP_LOGE(tag, "Parse error at line %" XML_FMT_INT_MOD "u:\n%" XML_FMT_STR "\n",
                        XML_GetCurrentLineNumber(parser),
                        XML_ErrorString(XML_GetErrorCode(parser)));
                } else {
                }
                XML_ParserFree(parser);
            }
        }
        close(mysocket);
        return ESP_FAIL;
    }
    return ESP_OK;
}
#endif
