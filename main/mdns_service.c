#include "mdns_service.h"
#include "mdns.h"
#include "esp_log.h"

static const char *TAG = "mdns_service";

// 启动 mDNS 服务
esp_err_t mdns_service_start(void)
{
    // 初始化 mDNS
    esp_err_t err = mdns_init();
    if (err) {
        ESP_LOGE(TAG, "mDNS Init failed: %d", err);
        return err;
    }

    // 设置主机名（可以通过 esp32cam.local 访问）
    mdns_hostname_set("esp32cam");

    // 设置实例名称
    mdns_instance_name_set("ESP32-CAM Video Stream");

    // 添加 HTTP 服务
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

    // 添加自定义服务信息
    mdns_txt_item_t serviceTxtData[3] = {
        {"board", "esp32s3"},
        {"path", "/stream"},
        {"version", "1.0"}
    };
    mdns_service_txt_set("_http", "_tcp", serviceTxtData, 3);

    ESP_LOGI(TAG, "mDNS service started");
    ESP_LOGI(TAG, "Hostname: esp32cam.local");
    ESP_LOGI(TAG, "Access via: http://esp32cam.local/");

    return ESP_OK;
}
