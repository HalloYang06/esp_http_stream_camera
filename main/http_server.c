#include "http_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "img_converters.h"
#include <string.h>
#include "bsp_camera.h"
static const char *TAG = "http_server";

// HTTP 服务器句柄
static httpd_handle_t camera_httpd = NULL;

// MJPEG 边界分隔符
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

// MJPEG 视频流处理器
static esp_err_t stream_handler(httpd_req_t *req)
{
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t * _jpg_buf = NULL;
    char part_buf[64];
    bool is_converted = false;

    ESP_LOGI(TAG, "Stream requested from client");

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK){
        return res;
    }

    while(true){
        // 从共享缓冲区获取最新帧
        camera_fb_t *fb = bsp_camera_get_frame(1000);
        if (!fb) {
            ESP_LOGE(TAG, "Failed to get frame from shared buffer");
            res = ESP_FAIL;
            break;
        }

        // RGB565 转 JPEG
        if(fb->format != PIXFORMAT_JPEG){
            is_converted = frame2jpg(fb, 50, &_jpg_buf, &_jpg_buf_len);
            if(!is_converted){
                ESP_LOGE(TAG, "JPEG compression failed");
                bsp_camera_frame_free(fb);
                res = ESP_FAIL;
                break;
            }
        } else {
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;
            is_converted = false;
        }

        // 发送 MJPEG 边界
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        // 发送 JPEG 头信息
        if(res == ESP_OK){
            size_t hlen = snprintf(part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        // 发送 JPEG 数据
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }

        // 释放转换后的 JPEG 缓冲区
        if(is_converted && _jpg_buf){
            free(_jpg_buf);
        }

        // 释放帧副本
        bsp_camera_frame_free(fb);

        if(res != ESP_OK){
            ESP_LOGI(TAG, "Stream client disconnected");
            break;
        }
    }

    return res;
}

// favicon 处理器
static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// 启动 HTTP 服务器
esp_err_t http_server_start(void)
{   

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_open_sockets = 2;
    config.max_uri_handlers = 4;
    config.lru_purge_enable = true;
    config.stack_size = 4096;  // 减小到 4KB
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_uri_t index_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t stream_uri = {
        .uri       = "/stream",
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t favicon_uri = {
        .uri       = "/favicon.ico",
        .method    = HTTP_GET,
        .handler   = favicon_handler,
        .user_ctx  = NULL
    };

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);
    ESP_LOGI(TAG, "httpd_config: port=%d, max_sockets=%d, stack_size=%d",
         config.server_port, config.max_open_sockets, config.stack_size);

    esp_err_t ret = httpd_start(&camera_httpd, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed with error: %s", esp_err_to_name(ret));
        return ret;
    }

    // 注册 URI 处理器
    ESP_LOGI(TAG, "HTTP server started, registering handlers");
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &stream_uri);
    httpd_register_uri_handler(camera_httpd, &favicon_uri);
    ESP_LOGI(TAG, "HTTP server started successfully");
    return ESP_OK;
}

// 停止 HTTP 服务器
void http_server_stop(void)
{
    if (camera_httpd) {
        httpd_stop(camera_httpd);
        camera_httpd = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
}
