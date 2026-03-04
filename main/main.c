#include "stdio.h"
#include "bsp_lichuang.h"
#include "bsp_lichuang.h"
#include "network.h"
#include "bsp_lcd.h"
#include "bsp_lvgl.h"
#include "bsp_ui.h"
#include "nvs_flash.h"
#include "esp_http_client.h"

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    const char *TAG = "app_main";
    esp_err_t ret;

    // 初始化 NVS（WiFi 需要）
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 临时：首次启动时自动保存 WiFi 配置（测试用）
    if (!wifi_is_configured()) {
        ESP_LOGI(TAG, "First boot, saving WiFi config...");
        wifi_save_config("Redmi K70 Pro", "88888888");  // 替换成你的 WiFi 名称和密码
    }

    // 检查是否已配置 WiFi
    if (wifi_is_configured()) {
        ESP_LOGI(TAG, "WiFi configured, connecting in STA mode");
        wifi_init_sta();
    } else {
        ESP_LOGI(TAG, "First boot, entering AP config mode");
        ESP_LOGI(TAG, "Connect to AP: ESP32-CAM-Setup");
        ESP_LOGI(TAG, "Password: 12345678");
        ESP_LOGI(TAG, "Then visit: http://192.168.4.1");
        wifi_init_smartconfig();

        // 配网模式下不初始化摄像头，等待配网完成后重启
        ESP_LOGI(TAG, "Config mode running, waiting for WiFi setup...");
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    ret=bsp_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C Init Failed:%s",esp_err_to_name(ret));
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

    // 初始化 LVGL
    ret = bsp_lvgl_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL Init Failed:%s", esp_err_to_name(ret));
        return;
    }

    // 启动 LVGL 任务
    ret = bsp_lvgl_start_tasks();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL Tasks Failed:%s", esp_err_to_name(ret));
        return;
    }

    // 初始化触摸输入
    ret = bsp_lvgl_touch_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL Touch Init Failed:%s", esp_err_to_name(ret));
        // 触摸失败不影响显示，继续运行
    }

    ESP_LOGI(TAG, "LVGL initialized successfully");

    // 等待一段时间，让硬件稳定
    vTaskDelay(pdMS_TO_TICKS(500));

    // 初始化摄像头（RGB565格式）
    ESP_LOGI(TAG, "Initializing camera...");
    ret=bsp_camera_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed:%s",esp_err_to_name(ret));

        // 摄像头失败时显示错误信息
        bsp_display_lock(0);
        lv_obj_t *label = lv_label_create(lv_screen_active());
        lv_label_set_text(label, "Camera Init Failed!\nCheck connections");
        lv_obj_set_style_text_color(label, lv_color_hex(0xFF0000), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
        bsp_display_unlock();

        ESP_LOGI(TAG, "System running without camera - touch screen to test");
        return;  // 继续运行，显示错误信息
    }

    // 创建摄像头采集任务
    ret = bsp_camera_tasks_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera Tasks Init Failed:%s", esp_err_to_name(ret));
        // 不要 return，继续初始化 UI
    } else {
        ESP_LOGI(TAG, "Camera initialized, LCD display controlled by button");
    }

    // 初始化 UI 界面（即使摄像头失败也要显示 UI）
    ret = bsp_ui_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UI Init Failed:%s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "UI initialized successfully");
    ESP_LOGI(TAG, "System ready - Touch buttons to interact");
/*
    // 测试网络连接（访问百度）
    ESP_LOGI(TAG, "Testing network connectivity...");
    esp_http_client_config_t test_config = {
        .url = "http://www.baidu.com",
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t test_client = esp_http_client_init(&test_config);
    if (test_client) {
        esp_err_t err = esp_http_client_perform(test_client);
        if (err == ESP_OK) {
            int status = esp_http_client_get_status_code(test_client);
            ESP_LOGI(TAG, "Network test OK, baidu.com returned: %d", status);
        } else {
            ESP_LOGE(TAG, "Network test FAILED: %s", esp_err_to_name(err));
        }
        esp_http_client_cleanup(test_client);
    }

    // 测试能否连接到上传服务器
    ESP_LOGI(TAG, "Testing upload server connectivity...");
    esp_http_client_config_t server_test_config = {
        .url = "http://115.190.73.223/xiaozhi/admin/images",
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t server_test_client = esp_http_client_init(&server_test_config);
    if (server_test_client) {
        esp_err_t err = esp_http_client_perform(server_test_client);
        if (err == ESP_OK) {
            int status = esp_http_client_get_status_code(server_test_client);
            ESP_LOGI(TAG, "Upload server test: status=%d", status);
        } else {
            ESP_LOGE(TAG, "Upload server test FAILED: %s", esp_err_to_name(err));
        }
        esp_http_client_cleanup(server_test_client);
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
    */

    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "System Init Complete!");
    ESP_LOGI(TAG, "Access: http://esp32cam.local/ (mDNS)");
    ESP_LOGI(TAG, "=================================================");
}