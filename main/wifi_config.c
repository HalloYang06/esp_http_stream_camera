#include "wifi_config.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "wifi_config";

// NVS 存储相关
#define NVS_NAMESPACE      "wifi_config"
#define NVS_SSID_KEY       "ssid"
#define NVS_PASSWORD_KEY   "password"

// 保存 WiFi 配置到 NVS
void wifi_config_save(const char *ssid, const char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return;
    }

    // 保存 SSID
    err = nvs_set_str(nvs_handle, NVS_SSID_KEY, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return;
    }

    // 保存密码
    err = nvs_set_str(nvs_handle, NVS_PASSWORD_KEY, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return;
    }

    // 提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing changes: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "WiFi config saved: SSID=%s", ssid);
    }

    nvs_close(nvs_handle);
}

// 从 NVS 加载 WiFi 配置
bool wifi_config_load(char *ssid, char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    size_t ssid_len = 32;
    size_t pass_len = 64;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No WiFi config found (first boot)");
        return false;
    }

    // 读取 SSID
    err = nvs_get_str(nvs_handle, NVS_SSID_KEY, ssid, &ssid_len);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "SSID not found in NVS");
        nvs_close(nvs_handle);
        return false;
    }

    // 读取密码
    err = nvs_get_str(nvs_handle, NVS_PASSWORD_KEY, password, &pass_len);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Password not found in NVS");
        nvs_close(nvs_handle);
        return false;
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "WiFi config loaded: SSID=%s", ssid);
    return true;
}

// 清除 WiFi 配置
void wifi_config_clear(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return;
    }

    nvs_erase_all(nvs_handle);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "WiFi config cleared");
}

// 检查是否已配置 WiFi
bool wifi_config_is_configured(void)
{
    char ssid[33];
    char password[65];
    return wifi_config_load(ssid, password);
}
