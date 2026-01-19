#include "network.h"
#include "stdio.h"
#define WIFI_SSID      "Redmi K70 Pro"
#define WIFI_PASS      "88888888"
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY          5
static int wifi_retry_num=0;

static void esp_event_handler(void* event_handler_arg,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                ESP_LOGI("WIFI", "WiFi started, connecting...");
                break;
            case WIFI_EVENT_STA_STOP:
                ESP_LOGI("WIFI", "WiFi stopped");
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI("WIFI", "Connected to AP SSID:%s, waiting for IP...", WIFI_SSID);
                wifi_retry_num = 0; // 重置重试计数
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                {
                    wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *)event_data;
                    ESP_LOGW("WIFI", "Disconnected from AP, reason: %d", disconnected->reason);

                    if(wifi_retry_num < MAX_RETRY){
                        esp_wifi_connect();
                        wifi_retry_num++;
                        ESP_LOGI("WIFI", "Retry to connect (attempt %d/%d)", wifi_retry_num, MAX_RETRY);
                    }else{
                        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                        ESP_LOGE("WIFI", "Failed to connect to SSID:%s after %d attempts", WIFI_SSID, MAX_RETRY);
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
                ESP_LOGI("WIFI", "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
                xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                break;
            default:
                break;
        }
    }
}
void wifi_init_sta(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    s_wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, 
                                                        ESP_EVENT_ANY_ID, 
                                                        &esp_event_handler, 
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                         IP_EVENT_STA_GOT_IP,
                                                         &esp_event_handler,
                                                         NULL,
                                                         &instance_got_ip));

    wifi_config_t wifi_config={
        .sta={
            .ssid=WIFI_SSID,
            .password=WIFI_PASS,
            .threshold.authmode=WIFI_AUTH_WPA2_PSK,

        }

    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI("WIFI", "WiFi Initialized");
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);
    if(bits & WIFI_CONNECTED_BIT){
        ESP_LOGI("WIFI", "Connected to AP");
    }else if(bits & WIFI_FAIL_BIT){
        ESP_LOGE("WIFI", "Failed to connect to AP");
    }else{
        ESP_LOGE("WIFI", "UNEXPECTED EVENT");
    }
}

/******************** HTTP服务器实现 ********************/
static httpd_handle_t camera_httpd = NULL;

// MJPEG边界分隔符
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// 主页处理器
static esp_err_t index_handler(httpd_req_t *req)
{
    const char* html =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<title>ESP32-CAM Stream</title>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<style>"
        "body{font-family:Arial;text-align:center;margin:20px;background:#f0f0f0;}"
        "h1{color:#333;}"
        "img{border:2px solid #333;border-radius:8px;max-width:100%;height:auto;}"
        ".container{max-width:800px;margin:0 auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}"
        "</style>"
        "</head>"
        "<body>"
        "<div class='container'>"
        "<h1>ESP32-CAM Live Stream</h1>"
        "<img src='/stream' id='stream'>"
        "<p><a href='/stream' target='_blank'>Open Stream in New Tab</a></p>"
        "<p style='color:#666;font-size:12px;'>Resolution: 320x240 | Format: MJPEG</p>"
        "</div>"
        "<script>"
        "var img=document.getElementById('stream');"
        "img.onerror=function(){setTimeout(function(){img.src='/stream?t='+Date.now();},1000);};"
        "</script>"
        "</body>"
        "</html>";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, strlen(html));
}

// MJPEG视频流处理器
static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t * _jpg_buf = NULL;
    char part_buf[64];
    bool is_converted = false;

    ESP_LOGI("HTTP", "Stream requested from client");

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK){
        return res;
    }

    while(true){
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE("HTTP", "Camera capture failed");
            res = ESP_FAIL;
            break;
        }

        // RGB565转JPEG（GC0308只支持RGB565）
        if(fb->format != PIXFORMAT_JPEG){
            // 降低质量到50，提升编码速度（60-80=高质量慢，40-50=中质量快，20-30=低质量很快）
            is_converted = frame2jpg(fb, 50, &_jpg_buf, &_jpg_buf_len);
            if(!is_converted){
                ESP_LOGE("HTTP", "JPEG compression failed");
                esp_camera_fb_return(fb);
                res = ESP_FAIL;
                break;
            }
        } else {
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;
            is_converted = false;
        }

        // 发送MJPEG边界
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        // 发送JPEG头信息
        if(res == ESP_OK){
            size_t hlen = snprintf(part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        // 发送JPEG数据
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }

        // 释放转换后的JPEG缓冲区
        if(is_converted && _jpg_buf){
            free(_jpg_buf);
        }
        esp_camera_fb_return(fb);

        if(res != ESP_OK){
            ESP_LOGI("HTTP", "Stream client disconnected");
            break;
        }
    }

    return res;
}

// 启动HTTP服务器
esp_err_t start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_open_sockets = 7;
    config.max_uri_handlers = 8;
    config.stack_size = 8192;

    // 注册主页URI
    httpd_uri_t index_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_handler,
        .user_ctx  = NULL
    };

    // 注册视频流URI
    httpd_uri_t stream_uri = {
        .uri       = "/stream",
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = NULL
    };

    ESP_LOGI("HTTP", "Starting HTTP server on port %d", config.server_port);

    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &stream_uri);
        ESP_LOGI("HTTP", "HTTP server started successfully");
        return ESP_OK;
    }

    ESP_LOGE("HTTP", "Failed to start HTTP server");
    return ESP_FAIL;
}

// 停止HTTP服务器
void stop_http_server(void)
{
    if (camera_httpd) {
        httpd_stop(camera_httpd);
        camera_httpd = NULL;
        ESP_LOGI("HTTP", "HTTP server stopped");
    }
}

/******************** mDNS服务实现 ********************/
esp_err_t start_mdns_service(void)
{
    // 初始化mDNS
    esp_err_t err = mdns_init();
    if (err) {
        ESP_LOGE("mDNS", "mDNS Init failed: %d", err);
        return err;
    }

    // 设置主机名（可以通过 esp32cam.local 访问）
    mdns_hostname_set("esp32cam");

    // 设置实例名称
    mdns_instance_name_set("ESP32-CAM Video Stream");

    // 添加HTTP服务
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

    // 添加自定义服务信息
    mdns_txt_item_t serviceTxtData[3] = {
        {"board", "esp32s3"},
        {"path", "/stream"},
        {"version", "1.0"}
    };
    mdns_service_txt_set("_http", "_tcp", serviceTxtData, 3);

    ESP_LOGI("mDNS", "mDNS service started");
    ESP_LOGI("mDNS", "Hostname: esp32cam.local");
    ESP_LOGI("mDNS", "Access via: http://esp32cam.local/");

    return ESP_OK;
}