#ifndef _WIFI_CONFIG_H_
#define _WIFI_CONFIG_H_

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 保存 WiFi 配置到 NVS
 * @param ssid SSID
 * @param password 密码
 */
void wifi_config_save(const char *ssid, const char *password);

/**
 * @brief 从 NVS 加载 WiFi 配置
 * @param ssid 输出 SSID 缓冲区（至少 33 字节）
 * @param password 输出密码缓冲区（至少 65 字节）
 * @return true 成功，false 失败
 */
bool wifi_config_load(char *ssid, char *password);

/**
 * @brief 清除 WiFi 配置
 */
void wifi_config_clear(void);

/**
 * @brief 检查是否已配置 WiFi
 * @return true 已配置，false 未配置
 */
bool wifi_config_is_configured(void);

#ifdef __cplusplus
}
#endif

#endif // _WIFI_CONFIG_H_
