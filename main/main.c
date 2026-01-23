#include "stdio.h"
#include "esp_cam.h"
#include "yingwu.h"
#include "network.h"
#
void app_main(void)
{

    esp_log_level_set("*", ESP_LOG_INFO);
    const char *TAG = "app_main";
    esp_err_t ret;

    // 检查是否已配置WiFi
    if (wifi_is_configured()) {
        ESP_LOGI(TAG, "WiFi已配置，使用STA模式连接");
        wifi_init_sta();
    } else {
        ESP_LOGI(TAG, "首次启动，进入AP配网模式");
        ESP_LOGI(TAG, "请连接热点: ESP32-CAM-Setup");
        ESP_LOGI(TAG, "密码: 12345678");
        ESP_LOGI(TAG, "然后访问: http://192.168.4.1");
        wifi_init_smartconfig();

        // 配网模式下不初始化摄像头，等待配网完成后重启
        ESP_LOGI(TAG, "配网模式运行中，等待用户配置WiFi...");
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    ret=bsp_iic_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IIC Init Failed:%s",esp_err_to_name(ret));
        return;
    }
    ret=pca9557_dev_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PCA9557 Dev Init Failed:%s",esp_err_to_name(ret));
        return;
    }
    pca9557_init();
    
    // 初始化LCD
    ret=lcd_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD Init Failed:%s",esp_err_to_name(ret));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    // 初始化摄像头（RGB565格式）
    ret=camera_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed:%s",esp_err_to_name(ret));
        return;
    }

    // 创建摄像头采集任务和LCD任务（基于共享缓冲区架构）
    ret = camera_tasks_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera Tasks Init Failed:%s", esp_err_to_name(ret));
        return;
    }

    // 启动mDNS服务（用于自动发现ESP32）
    ret = start_mdns_service();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "mDNS: You can access via http://esp32cam.local/");
    }

    // 启动HTTP服务器用于网络视频流
    ret = start_http_server();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP Server Failed:%s",esp_err_to_name(ret));
        // HTTP失败不影响LCD显示，继续运行
    }

    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "System Init Complete!");
    ESP_LOGI(TAG, "Access: http://esp32cam.local/ (mDNS)");
    ESP_LOGI(TAG, "=================================================");
}