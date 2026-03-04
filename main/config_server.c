#include "config_server.h"
#include "wifi_config.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include <string.h>

static const char *TAG = "config_server";

// 配网服务器句柄
static httpd_handle_t config_server = NULL;

// 配网主页处理器
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

// 保存 WiFi 配置处理器
static esp_err_t wifi_save_handler(httpd_req_t *req)
{
    char content[300];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);

    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }

    content[ret] = '\0';

    // 解析 SSID 和密码
    char ssid[33] = {0};
    char password[65] = {0};

    esp_err_t ssid_err = httpd_query_key_value(content, "ssid", ssid, sizeof(ssid));
    esp_err_t pass_err = httpd_query_key_value(content, "password", password, sizeof(password));

    if (ssid_err == ESP_OK && pass_err == ESP_OK) {
        ESP_LOGI(TAG, "Received WiFi config - SSID: %s", ssid);

        if (strlen(ssid) == 0) {
            ESP_LOGE(TAG, "SSID cannot be empty");
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        // 保存到 NVS
        wifi_config_save(ssid, password);

        // 发送成功响应
        const char* resp = "OK";
        httpd_resp_send(req, resp, strlen(resp));

        // 停止配网服务器
        if (config_server != NULL) {
            httpd_stop(config_server);
            config_server = NULL;
        }

        // 延迟重启以切换到 STA 模式
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();

        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to parse WiFi credentials");
    httpd_resp_send_500(req);
    return ESP_FAIL;
}

// 启动配网 HTTP 服务器
esp_err_t config_server_start(void)
{
    // 如果服务器已经在运行，先停止
    if (config_server != NULL) {
        httpd_stop(config_server);
        config_server = NULL;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 4;
    config.stack_size = 4096;  // 减小到 4KB
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

    ESP_LOGI(TAG, "Starting config server on port 80");

    if (httpd_start(&config_server, &config) == ESP_OK) {
        httpd_register_uri_handler(config_server, &config_page_uri);
        httpd_register_uri_handler(config_server, &wifi_save_uri);
        ESP_LOGI(TAG, "Config server started");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to start config server");
    return ESP_FAIL;
}

// 停止配网 HTTP 服务器
void config_server_stop(void)
{
    if (config_server) {
        httpd_stop(config_server);
        config_server = NULL;
        ESP_LOGI(TAG, "Config server stopped");
    }
}
