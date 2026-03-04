#ifndef NETWORK_H
#define NETWORK_H

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "mdns.h"  
#include "bsp_lcd.h"
#include "esp_cam.h"
// WiFi配网功能
void wifi_init_smartconfig(void);
bool wifi_is_configured(void);
void wifi_save_config(const char *ssid, const char *password);
void wifi_clear_config(void);


void wifi_init_sta(void);
esp_err_t start_http_server(void);
void stop_http_server(void);
esp_err_t start_mdns_service(void);//启用·mdns服务

#endif