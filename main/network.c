#include "network.h"
#include "stdio.h"

// NVS存储相关
#define NVS_NAMESPACE      "wifi_config"
#define NVS_SSID_KEY       "ssid"
#define NVS_PASSWORD_KEY   "password"

// 默认AP配置
#define DEFAULT_AP_SSID    "ESP32-CAM-Setup"
#define DEFAULT_AP_PASS    "12345678"
#define DEFAULT_AP_CHANNEL 1
#define DEFAULT_AP_MAX_CONN 4

// WiFi连接配置
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY          5
static int wifi_retry_num=0;

// 配网服务器句柄
static httpd_handle_t config_server = NULL;

/******************** NVS WiFi配置存储 ********************/
// 保存WiFi配置到NVS
void wifi_save_config(const char *ssid, const char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Error opening NVS handle: %s", esp_err_to_name(err));
        return;
    }

    // 保存SSID
    err = nvs_set_str(nvs_handle, NVS_SSID_KEY, ssid);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Error saving SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return;
    }

    // 保存密码
    err = nvs_set_str(nvs_handle, NVS_PASSWORD_KEY, password);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Error saving password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return;
    }

    // 提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Error committing changes: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI("NVS", "WiFi config saved: SSID=%s", ssid);
    }

    nvs_close(nvs_handle);
}

// 从NVS读取WiFi配置
bool wifi_load_config(char *ssid, char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    size_t ssid_len = 32;
    size_t pass_len = 64;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI("NVS", "No WiFi config found (first boot)");
        return false;
    }

    // 读取SSID
    err = nvs_get_str(nvs_handle, NVS_SSID_KEY, ssid, &ssid_len);
    if (err != ESP_OK) {
        ESP_LOGI("NVS", "SSID not found in NVS");
        nvs_close(nvs_handle);
        return false;
    }

    // 读取密码
    err = nvs_get_str(nvs_handle, NVS_PASSWORD_KEY, password, &pass_len);
    if (err != ESP_OK) {
        ESP_LOGI("NVS", "Password not found in NVS");
        nvs_close(nvs_handle);
        return false;
    }

    nvs_close(nvs_handle);
    ESP_LOGI("NVS", "WiFi config loaded: SSID=%s", ssid);
    return true;
}

// 清除WiFi配置
void wifi_clear_config(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_erase_all(nvs_handle);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI("NVS", "WiFi config cleared");
    }
}

// 检查是否已配置WiFi
bool wifi_is_configured(void)
{
    char ssid[32];
    char password[64];
    return wifi_load_config(ssid, password);
}

/******************** WiFi事件处理 ********************/
static void esp_event_handler(void* event_handler_arg,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_STACONNECTED:
                {
                    wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
                    ESP_LOGI("WIFI", "Station " MACSTR " joined, AID=%d",
                             MAC2STR(event->mac), event->aid);
                }
                break;
            case WIFI_EVENT_AP_STADISCONNECTED:
                {
                    wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
                    ESP_LOGI("WIFI", "Station " MACSTR " left, AID=%d",
                             MAC2STR(event->mac), event->aid);
                }
                break;
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                ESP_LOGI("WIFI", "WiFi started, connecting...");
                break;
            case WIFI_EVENT_STA_STOP:
                ESP_LOGI("WIFI", "WiFi stopped");
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI("WIFI", "Connected to AP, waiting for IP...");
                wifi_retry_num = 0;
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
                        ESP_LOGE("WIFI", "Failed to connect after %d attempts", MAX_RETRY);
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

/******************** WiFi配网网页处理器 ********************/
// 配网主页
static esp_err_t config_page_handler(httpd_req_t *req)
{
    const char* html =
        "<!DOCTYPE html>"
        "<html lang='zh-CN'>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>ESP32-CAM WiFi配网</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);margin:0;padding:20px;min-height:100vh;display:flex;align-items:center;justify-content:center;}"
        ".container{background:white;border-radius:15px;box-shadow:0 10px 40px rgba(0,0,0,0.2);padding:30px;max-width:400px;width:100%;}"
        "h1{color:#333;text-align:center;margin-bottom:10px;font-size:24px;}"
        ".subtitle{text-align:center;color:#666;font-size:14px;margin-bottom:25px;}"
        ".form-group{margin-bottom:20px;}"
        "label{display:block;color:#555;font-weight:bold;margin-bottom:8px;font-size:14px;}"
        "input{width:100%;padding:12px;border:2px solid #e0e0e0;border-radius:8px;font-size:14px;box-sizing:border-box;transition:border-color 0.3s;}"
        "input:focus{outline:none;border-color:#667eea;}"
        "button{width:100%;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);color:white;border:none;padding:14px;border-radius:8px;font-size:16px;font-weight:bold;cursor:pointer;transition:transform 0.2s,box-shadow 0.2s;}"
        "button:hover{transform:translateY(-2px);box-shadow:0 5px 15px rgba(102,126,234,0.4);}"
        "button:active{transform:translateY(0);}"
        ".info{background:#f0f7ff;border-left:4px solid #667eea;padding:12px;border-radius:5px;margin-bottom:20px;font-size:13px;color:#555;}"
        ".status{text-align:center;margin-top:15px;font-size:14px;color:#666;min-height:20px;}"
        ".success{color:#28a745;font-weight:bold;}"
        ".error{color:#dc3545;font-weight:bold;}"
        "</style>"
        "</head>"
        "<body>"
        "<div class='container'>"
        "<h1>ESP32-CAM</h1>"
        "<div class='subtitle'>WiFi网络配置</div>"
        "<div class='info'>"
        "请输入您的家庭WiFi信息，ESP32将自动连接并切换到工作模式"
        "</div>"
        "<form id='wifiForm' onsubmit='return submitForm(event)'>"
        "<div class='form-group'>"
        "<label for='ssid'>WiFi名称 (SSID)</label>"
        "<input type='text' id='ssid' name='ssid' placeholder='请输入WiFi名称' required maxlength='31'>"
        "</div>"
        "<div class='form-group'>"
        "<label for='password'>WiFi密码</label>"
        "<input type='password' id='password' name='password' placeholder='请输入WiFi密码' required maxlength='63'>"
        "</div>"
        "<button type='submit'>保存并连接</button>"
        "</form>"
        "<div class='status' id='status'></div>"
        "</div>"
        "<script>"
        "function submitForm(e){"
        "e.preventDefault();"
        "var ssid=document.getElementById('ssid').value;"
        "var password=document.getElementById('password').value;"
        "var status=document.getElementById('status');"
        "status.className='status';"
        "status.textContent='正在保存配置...';"
        "fetch('/wifi/save',{"
        "method:'POST',"
        "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "body:'ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(password)"
        "}).then(r=>r.text()).then(data=>{"
        "status.className='status success';"
        "status.textContent='配置成功！设备将重启并连接到WiFi...';"
        "setTimeout(()=>{"
        "status.textContent='您可以关闭此页面，稍后通过新WiFi网络访问设备';"
        "},2000);"
        "}).catch(err=>{"
        "status.className='status error';"
        "status.textContent='配置失败，请重试';"
        "});"
        "return false;"
        "}"
        "</script>"
        "</body>"
        "</html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "identity");
    return httpd_resp_send(req, html, strlen(html));
}

// 保存WiFi配置
static esp_err_t wifi_save_handler(httpd_req_t *req)
{
    char content[300];  // 增加缓冲区大小，SSID(31)+密码(63) URL编码后约200字节
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);

    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }

    content[ret] = '\0';

    // 解析SSID和密码
    char ssid[33] = {0};  // WiFi SSID最大32字节+1字节结束符
    char password[65] = {0};  // WiFi密码最大64字节+1字节结束符

    // 使用httpd_query_key_value进行URL解码
    esp_err_t ssid_err = httpd_query_key_value(content, "ssid", ssid, sizeof(ssid));
    esp_err_t pass_err = httpd_query_key_value(content, "password", password, sizeof(password));

    if (ssid_err == ESP_OK && pass_err == ESP_OK) {
        ESP_LOGI("CONFIG", "Received WiFi config - SSID: %s", ssid);

        // 验证SSID不为空
        if (strlen(ssid) == 0) {
            ESP_LOGE("CONFIG", "SSID cannot be empty");
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        // 保存到NVS
        wifi_save_config(ssid, password);

        // 发送成功响应
        const char* resp = "OK";
        httpd_resp_send(req, resp, strlen(resp));

        // 停止配网服务器
        if (config_server != NULL) {
            httpd_stop(config_server);
            config_server = NULL;
        }

        // 延迟重启以切换到STA模式
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();

        return ESP_OK;
    }

    ESP_LOGE("CONFIG", "Failed to parse WiFi credentials");
    httpd_resp_send_500(req);
    return ESP_FAIL;
}

// 启动配网HTTP服务器
static esp_err_t start_config_server(void)
{
    // 如果服务器已经在运行，先停止
    if (config_server != NULL) {
        httpd_stop(config_server);
        config_server = NULL;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 4;
    config.stack_size = 8192;  // 增加栈大小
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_uri_t config_page_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = config_page_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t wifi_save_uri = {
        .uri       = "/wifi/save",
        .method    = HTTP_POST,
        .handler   = wifi_save_handler,
        .user_ctx  = NULL
    };

    ESP_LOGI("CONFIG", "Starting config server on port 80");

    if (httpd_start(&config_server, &config) == ESP_OK) {
        httpd_register_uri_handler(config_server, &config_page_uri);
        httpd_register_uri_handler(config_server, &wifi_save_uri);
        ESP_LOGI("CONFIG", "Config server started");
        return ESP_OK;
    }

    ESP_LOGE("CONFIG", "Failed to start config server");
    return ESP_FAIL;
}

/******************** WiFi初始化 ********************/
// STA模式初始化（使用保存的凭据）
void wifi_init_sta(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE("WIFI", "Failed to create event group");
        return;
    }

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

    // 从NVS读取WiFi配置
    char ssid[33] = {0};
    char password[65] = {0};

    if (!wifi_load_config(ssid, password)) {
        ESP_LOGE("WIFI", "No WiFi config found, cannot connect");
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
        return;
    }

    wifi_config_t wifi_config = {0};
    // 使用strncpy并确保字符串以null结尾
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';

    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';

    // 根据密码设置认证模式
    if (strlen(password) > 0) {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI("WIFI", "STA Mode - Connecting to SSID: %s", ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if(bits & WIFI_CONNECTED_BIT){
        ESP_LOGI("WIFI", "Connected to AP successfully");
    }else if(bits & WIFI_FAIL_BIT){
        ESP_LOGE("WIFI", "Failed to connect to AP");
    }else{
        ESP_LOGE("WIFI", "UNEXPECTED EVENT");
    }
}

// AP模式初始化（用于配网）
void wifi_init_smartconfig(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE("WIFI", "Failed to create event group");
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &esp_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = DEFAULT_AP_SSID,
            .ssid_len = strlen(DEFAULT_AP_SSID),
            .channel = DEFAULT_AP_CHANNEL,
            .password = DEFAULT_AP_PASS,
            .max_connection = DEFAULT_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK
        },
    };

    if (strlen(DEFAULT_AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI("WIFI", "AP Mode Started");
    ESP_LOGI("WIFI", "SSID: %s", DEFAULT_AP_SSID);
    ESP_LOGI("WIFI", "Password: %s", DEFAULT_AP_PASS);
    ESP_LOGI("WIFI", "IP: 192.168.4.1");
    ESP_LOGI("WIFI", "Please connect to this AP and visit http://192.168.4.1");

    // 启动配网服务器
    ret = start_config_server();
    if (ret != ESP_OK) {
        ESP_LOGE("WIFI", "Failed to start config server");
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

// MJPEG视频流处理器（使用共享帧缓冲区）
static esp_err_t stream_handler(httpd_req_t *req)
{
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
        // 从共享缓冲区获取最新帧（等待最多1000ms）
        camera_fb_t *fb = camera_get_latest_frame(1000);
        if (!fb) {
            ESP_LOGE("HTTP", "Failed to get frame from shared buffer");
            res = ESP_FAIL;
            break;
        }

        // RGB565转JPEG（GC0308只支持RGB565）
        if(fb->format != PIXFORMAT_JPEG){
            // 降低质量到50，提升编码速度（60-80=高质量慢，40-50=中质量快，20-30=低质量很快）
            is_converted = frame2jpg(fb, 50, &_jpg_buf, &_jpg_buf_len);
            if(!is_converted){
                ESP_LOGE("HTTP", "JPEG compression failed");
                camera_frame_release(fb);
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

        // 释放帧副本
        camera_frame_release(fb);

        if(res != ESP_OK){
            ESP_LOGI("HTTP", "Stream client disconnected");
            break;
        }
    }

    return res;
}

// favicon处理器（避免404错误）
static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// 启动HTTP服务器
esp_err_t start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_open_sockets = 7;
    config.max_uri_handlers = 8;
    config.stack_size = 12288;  // 增加栈大小到12KB
    config.uri_match_fn = httpd_uri_match_wildcard;

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

    // 注册favicon URI
    httpd_uri_t favicon_uri = {
        .uri       = "/favicon.ico",
        .method    = HTTP_GET,
        .handler   = favicon_handler,
        .user_ctx  = NULL
    };

    ESP_LOGI("HTTP", "Starting HTTP server on port %d", config.server_port);

    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &stream_uri);
        httpd_register_uri_handler(camera_httpd, &favicon_uri);
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