#include "wifi_manager.h"
#include "wifi_config.h"
#include "config_server.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi_manager";

// WiFi 连接配置
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY          5
static int wifi_retry_num = 0;

// 默认 AP 配置
#define DEFAULT_AP_SSID    "ESP32-CAM-Setup"
#define DEFAULT_AP_PASS    "12345678"
#define DEFAULT_AP_CHANNEL 1
#define DEFAULT_AP_MAX_CONN 4

// WiFi 事件处理
static void wifi_event_handler(void* event_handler_arg,
                                esp_event_base_t event_base,
                                int32_t event_id,
                                void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_STACONNECTED:
                {
                    wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
                    ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d",
                             MAC2STR(event->mac), event->aid);
                }
                break;
            case WIFI_EVENT_AP_STADISCONNECTED:
                {
                    wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
                    ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d",
                             MAC2STR(event->mac), event->aid);
                }
                break;
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                ESP_LOGI(TAG, "WiFi started, connecting...");
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Connected to AP, waiting for IP...");
                wifi_retry_num = 0;
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                {
                    wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *)event_data;
                    ESP_LOGW(TAG, "Disconnected from AP, reason: %d", disconnected->reason);

                    if(wifi_retry_num < MAX_RETRY){
                        esp_wifi_connect();
                        wifi_retry_num++;
                        ESP_LOGI(TAG, "Retry to connect (attempt %d/%d)", wifi_retry_num, MAX_RETRY);
                    }else{
                        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                        ESP_LOGE(TAG, "Failed to connect after %d attempts", MAX_RETRY);
                    }
                }
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP:
                wifi_retry_num = 0;
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
                xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                break;
            default:
                break;
        }
    }
}

// WiFi 扫描功能
uint16_t wifi_manager_scan(wifi_scan_result_t *results, uint16_t max_results)
{
    if (!results || max_results == 0) {
        return 0;
    }

    ESP_LOGI(TAG, "Starting WiFi scan...");

    // 配置扫描参数
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    // 开始扫描
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(err));
        return 0;
    }

    // 获取扫描结果数量
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count == 0) {
        ESP_LOGI(TAG, "No APs found");
        return 0;
    }

    ESP_LOGI(TAG, "Found %d APs", ap_count);

    // 限制结果数量
    uint16_t result_count = (ap_count < max_results) ? ap_count : max_results;

    // 分配临时缓冲区
    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * result_count);
    if (!ap_records) {
        ESP_LOGE(TAG, "Failed to allocate memory for scan results");
        return 0;
    }

    // 获取扫描结果
    err = esp_wifi_scan_get_ap_records(&result_count, ap_records);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get scan results: %s", esp_err_to_name(err));
        free(ap_records);
        return 0;
    }

    // 复制结果到输出数组
    for (uint16_t i = 0; i < result_count; i++) {
        strncpy(results[i].ssid, (char *)ap_records[i].ssid, 32);
        results[i].ssid[32] = '\0';
        results[i].rssi = ap_records[i].rssi;
        results[i].authmode = ap_records[i].authmode;

        ESP_LOGI(TAG, "  [%d] SSID: %s, RSSI: %d, Auth: %d",
                 i, results[i].ssid, results[i].rssi, results[i].authmode);
    }

    free(ap_records);
    return result_count;
}

// 初始化 STA 模式
void wifi_manager_init_sta(void)
{
    char ssid[33];
    char password[65];

    if (!wifi_config_load(ssid, password)) {
        ESP_LOGE(TAG, "No WiFi config found");
        return;
    }

    wifi_manager_init(WIFI_MODE_STA, ssid, password);
}

// 初始化 SmartConfig 模式
void wifi_manager_init_smartconfig(void)
{
    wifi_manager_init(WIFI_MODE_AP, NULL, NULL);
}

// 统一的 WiFi 初始化接口
esp_err_t wifi_manager_init(wifi_mode_t mode, const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Initializing WiFi in mode: %d", mode);

    switch (mode) {
    case WIFI_MODE_STA:
        // Station 模式
        if (!ssid || !password) {
            ESP_LOGE(TAG, "SSID and password required for STA mode");
            return ESP_FAIL;
        }

        // 初始化网络接口
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();

        // WiFi 配置
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        // 注册事件处理
        s_wifi_event_group = xEventGroupCreate();
        esp_event_handler_instance_t instance_any_id;
        esp_event_handler_instance_t instance_got_ip;
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &wifi_event_handler,
                                                            NULL,
                                                            &instance_any_id));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                            IP_EVENT_STA_GOT_IP,
                                                            &wifi_event_handler,
                                                            NULL,
                                                            &instance_got_ip));

        // 配置 WiFi
        wifi_config_t wifi_config = {
            .sta = {
                .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                .pmf_cfg = {
                    .capable = true,
                    .required = false
                },
            },
        };
        strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);

        // 等待连接
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                               WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                               pdFALSE,
                                               pdFALSE,
                                               portMAX_DELAY);

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "Connected to AP");
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "Failed to connect to AP");
            return ESP_FAIL;
        }
        break;

    case WIFI_MODE_AP:
        // AP 模式 - 启动配网服务器
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_ap();

        wifi_init_config_t cfg2 = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg2));

        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &wifi_event_handler,
                                                            NULL,
                                                            NULL));

        wifi_config_t ap_config = {
            .ap = {
                .ssid = DEFAULT_AP_SSID,
                .ssid_len = strlen(DEFAULT_AP_SSID),
                .channel = DEFAULT_AP_CHANNEL,
                .password = DEFAULT_AP_PASS,
                .max_connection = DEFAULT_AP_MAX_CONN,
                .authmode = WIFI_AUTH_WPA_WPA2_PSK
            },
        };

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        ESP_LOGI(TAG, "WiFi AP started. SSID:%s password:%s", DEFAULT_AP_SSID, DEFAULT_AP_PASS);

        // 启动配网服务器
        config_server_start();
        return ESP_OK;

    case WIFI_MODE_APSTA:
        ESP_LOGW(TAG, "AP+STA mode not implemented yet");
        return ESP_FAIL;

    default:
        ESP_LOGE(TAG, "Unknown WiFi mode: %d", mode);
        return ESP_FAIL;
    }

    return ESP_OK;
}
