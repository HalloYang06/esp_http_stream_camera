#ifndef NETWORK_H
#define NETWORK_H

// 统一网络模块头文件 - 包含所有子模块
#include "wifi_config.h"
#include "wifi_manager.h"
#include "http_server.h"
#include "config_server.h"
#include "mdns_service.h"
#include "bsp_lichuang.h"

// ==================== 向后兼容的旧接口 ====================
// 这些函数映射到新的模块化接口

/**
 * @brief 启动 HTTP 服务器（兼容旧接口）
 * @return ESP_OK 成功，其他失败
 */
static inline esp_err_t start_http_server(void) {
    return http_server_start();
}

/**
 * @brief 停止 HTTP 服务器（兼容旧接口）
 */
static inline void stop_http_server(void) {
    http_server_stop();
}

/**
 * @brief 启动 mDNS 服务（兼容旧接口）
 * @return ESP_OK 成功，其他失败
 */
static inline esp_err_t start_mdns_service(void) {
    return mdns_service_start();
}

/**
 * @brief 保存 WiFi 配置到 NVS（兼容旧接口）
 * @param ssid SSID
 * @param password 密码
 */
static inline void wifi_save_config(const char *ssid, const char *password) {
    wifi_config_save(ssid, password);
}

/**
 * @brief 清除 WiFi 配置（兼容旧接口）
 */
static inline void wifi_clear_config(void) {
    wifi_config_clear();
}

/**
 * @brief 检查 WiFi 是否已配置（兼容旧接口）
 * @return true 已配置，false 未配置
 */
static inline bool wifi_is_configured(void) {
    return wifi_config_is_configured();
}

/**
 * @brief 扫描 WiFi（兼容旧接口）
 * @param results 扫描结果数组
 * @param max_results 最大结果数
 * @return 实际扫描到的 WiFi 数量
 */
static inline uint16_t wifi_scan(wifi_scan_result_t *results, uint16_t max_results) {
    return wifi_manager_scan(results, max_results);
}

/**
 * @brief 初始化 WiFi（兼容旧接口）
 * @param mode WiFi 模式
 * @param ssid SSID（STA 模式需要，其他模式可为 NULL）
 * @param password 密码（STA 模式需要，其他模式可为 NULL）
 * @return ESP_OK 成功，其他失败
 */
static inline esp_err_t wifi_init(wifi_mode_t mode, const char *ssid, const char *password) {
    return wifi_manager_init(mode, ssid, password);
}

/**
 * @brief 初始化 STA 模式（兼容旧接口）
 */
static inline void wifi_init_sta(void) {
    wifi_manager_init_sta();
}

/**
 * @brief 初始化 SmartConfig 模式（兼容旧接口）
 */
static inline void wifi_init_smartconfig(void) {
    wifi_manager_init_smartconfig();
}

#endif