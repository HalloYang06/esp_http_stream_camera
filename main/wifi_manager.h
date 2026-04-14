#ifndef _WIFI_MANAGER_H_
#define _WIFI_MANAGER_H_

#include "esp_err.h"
#include "esp_wifi.h"
#include <stdint.h>
#include "esp_mac.h"
#ifdef __cplusplus
extern "C" {
#endif



// WiFi 扫描结果结构
typedef struct {
    char ssid[33];
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_scan_result_t;

/**
 * @brief 初始化 WiFi
 * @param mode WiFi 模式
 * @param ssid SSID（STA 模式需要，其他模式可为 NULL）
 * @param password 密码（STA 模式需要，其他模式可为 NULL）
 * @return ESP_OK 成功，其他失败
 */
esp_err_t wifi_manager_init(wifi_mode_t mode, const char *ssid, const char *password);

/**
 * @brief 扫描 WiFi
 * @param results 扫描结果数组
 * @param max_results 最大结果数
 * @return 实际扫描到的 WiFi 数量
 */
uint16_t wifi_manager_scan(wifi_scan_result_t *results, uint16_t max_results);

/**
 * @brief 初始化 STA 模式（旧接口，兼容性）
 */
void wifi_manager_init_sta(void);

/**
 * @brief 初始化 SmartConfig 模式（旧接口，兼容性）
 */
void wifi_manager_init_smartconfig(void);

#ifdef __cplusplus
}
#endif

#endif // _WIFI_MANAGER_H_
