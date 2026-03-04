#include "bsp_ui.h"
#include "bsp_lvgl.h"
#include "bsp_camera.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "img_converters.h"
#include <string.h>
#include <inttypes.h>
static const char *TAG = "bsp_ui";
LV_FONT_DECLARE(lv_font_siyuanbold_jibenhanzi_16);
size_t jpeg_len = 0;
// UI 控件
static lv_obj_t *btn_capture = NULL;
static lv_obj_t *btn_start = NULL;
static lv_obj_t *btn_cancel = NULL;
static lv_obj_t *label_status = NULL;
static lv_obj_t *textarea_result = NULL;
static lv_obj_t *camera_canvas = NULL;  // 使用 canvas 代替 image

// 页面状态
static bool camera_view_active = false;

// Canvas 缓冲区（PSRAM）
static uint8_t *canvas_buffer = NULL;

// 摄像头显示任务句柄
static TaskHandle_t camera_display_task_handle = NULL;

// API 配置
#define SERVER_HOST "115.190.73.223"
#define UPLOAD_ENDPOINT "/xiaozhi/admin/images"
#define GLM_SERVER_HOST "10.121.121.110"  // Python 服务器 IP
#define GLM_SERVER_PORT "5000"
#define GLM_ANALYZE_ENDPOINT "/analyze"
#define GLM_API_URL "https://open.bigmodel.cn/api/paas/v4/chat/completions"
#define API_KEY "640874cba211c88cbfdba211e005a15f.n5aU0HZDrTmYd2ji"
#define QUESTION "图片里的主要内容是什么? 如果图片里有人物, 描述下这个人物在干嘛"

// HTTP 响应上下文（每个请求独立）
typedef struct {
    char *response_buf;
    size_t response_len;
} http_response_ctx_t;

// HTTP 事件处理（使用独立上下文）
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_ctx_t *ctx = (http_response_ctx_t *)evt->user_data;
    if (ctx == NULL) {
        return ESP_FAIL;
    }

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (ctx->response_buf == NULL) {
            ctx->response_buf = malloc(evt->data_len + 1);
            if (ctx->response_buf) {
                memcpy(ctx->response_buf, evt->data, evt->data_len);
                ctx->response_len = evt->data_len;
                ctx->response_buf[ctx->response_len] = '\0';
            } else {
                ESP_LOGE(TAG, "Failed to allocate response buffer");
                return ESP_FAIL;
            }
        } else {
            char *new_buffer = realloc(ctx->response_buf, ctx->response_len + evt->data_len + 1);
            if (new_buffer) {
                ctx->response_buf = new_buffer;
                memcpy(ctx->response_buf + ctx->response_len, evt->data, evt->data_len);
                ctx->response_len += evt->data_len;
                ctx->response_buf[ctx->response_len] = '\0';
            } else {
                ESP_LOGE(TAG, "Failed to realloc response buffer");
                return ESP_FAIL;
            }
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

// 上传图片到服务器（使用独立上下文）
static char* upload_image_to_server(const uint8_t *image_data, size_t image_len)
{
    ESP_LOGI(TAG, "=== Starting image upload ===");
    ESP_LOGI(TAG, "Image size: %zu bytes", image_len);

    // 每个请求独立的响应上下文，绝对不共用全局变量
    http_response_ctx_t resp_ctx = {
        .response_buf = NULL,
        .response_len = 0
    };

    // ========== 1. 安全构建 multipart/form-data ==========
    const char *boundary = "----ESP32-Camera-Boundary-1234567890";
    char header_part[512] = {0}; // 初始化为0，避免脏数据
    char footer_part[128] = {0};

    // 用snprintf的返回值校验是否溢出
    int header_len = snprintf(header_part, sizeof(header_part),
             "--%s\r\n"
             "Content-Disposition: form-data; name=\"file\"; filename=\"capture.jpg\"\r\n"
             "Content-Type: image/jpeg\r\n\r\n",
             boundary);
    // 校验：如果header_len超过缓冲区大小，直接报错退出，避免溢出
    if (header_len < 0 || header_len >= sizeof(header_part)) {
        ESP_LOGE(TAG, "Header overflow, len=%d", header_len);
        return NULL;
    }

    int footer_len = snprintf(footer_part, sizeof(footer_part), "\r\n--%s--\r\n", boundary);
    if (footer_len < 0 || footer_len >= sizeof(footer_part)) {
        ESP_LOGE(TAG, "Footer overflow, len=%d", footer_len);
        return NULL;
    }

    // 计算总长度，绝对准确
    size_t total_len = header_len + image_len + footer_len;
    ESP_LOGI(TAG, "Total POST data size: %zu bytes", total_len);

    // 分配POST缓冲区，严格校验分配结果
    char *post_data = malloc(total_len);
    if (!post_data) {
        ESP_LOGE(TAG, "Failed to allocate POST data buffer, need %zu bytes", total_len);
        return NULL;
    }

    // 安全组装数据，用准确的长度，不用strlen（避免字符串里有0截断）
    size_t offset = 0;
    memcpy(post_data + offset, header_part, header_len);
    offset += header_len;
    memcpy(post_data + offset, image_data, image_len);
    offset += image_len;
    memcpy(post_data + offset, footer_part, footer_len);

    // ========== 2. 配置HTTP客户端 ==========
    char url[256] = {0};
    snprintf(url, sizeof(url), "http://%s%s", SERVER_HOST, UPLOAD_ENDPOINT);
    ESP_LOGI(TAG, "Upload URL: %s", url);

    char content_type[128] = {0};
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);

    // HTTP配置，把独立的响应上下文传入user_data
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp_ctx, // 关键：每个请求独立的上下文，互不干扰
        .timeout_ms = 30000,
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
        .keep_alive_enable = true,
        .disable_auto_redirect = false,
    };

    // ========== 3. 执行HTTP请求 ==========
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        free(post_data);
        return NULL;
    }

    esp_http_client_set_header(client, "Content-Type", content_type);
    esp_http_client_set_post_field(client, post_data, total_len);

    ESP_LOGI(TAG, "Performing HTTP POST...");
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP POST result: err=%s, status_code=%d", esp_err_to_name(err), status_code);

    // ========== 4. 清理POST缓冲区（用完立即释放，绝不泄漏） ==========
    free(post_data);
    post_data = NULL;
    esp_http_client_cleanup(client);
    client = NULL;

    // ========== 5. 错误处理 ==========
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
        if (resp_ctx.response_buf != NULL) {
            free(resp_ctx.response_buf);
        }
        return NULL;
    }

    if (status_code != 200) {
        ESP_LOGE(TAG, "Server returned error code: %d", status_code);
        if (resp_ctx.response_buf != NULL) {
            ESP_LOGE(TAG, "Server response: %s", resp_ctx.response_buf);
            free(resp_ctx.response_buf);
        }
        return NULL;
    }

    ESP_LOGI(TAG, "Upload successful! Status code: 200");
    char *full_url = NULL;

    // ========== 6. 解析JSON，获取图片URL ==========
    if (resp_ctx.response_buf != NULL && resp_ctx.response_len > 0) {
        ESP_LOGI(TAG, "Server response: %s", resp_ctx.response_buf);

        cJSON *json = cJSON_Parse(resp_ctx.response_buf);
        if (json != NULL) {
            cJSON *url_item = cJSON_GetObjectItem(json, "url");
            if (url_item != NULL && cJSON_IsString(url_item)) {
                // 分配URL缓冲区，256字节足够，严格校验
                full_url = malloc(256);
                if (full_url != NULL) {
                    snprintf(full_url, 256, "http://%s%s", SERVER_HOST, url_item->valuestring);
                    ESP_LOGI(TAG, "Image URL: %s", full_url);
                }
            } else {
                ESP_LOGE(TAG, "No 'url' field in server response");
            }
            cJSON_Delete(json);
        } else {
            ESP_LOGE(TAG, "Failed to parse JSON response");
        }
    }

    // ========== 7. 释放响应缓冲区，用完就放，绝不残留 ==========
    if (resp_ctx.response_buf != NULL) {
        free(resp_ctx.response_buf);
        resp_ctx.response_buf = NULL;
    }

    return full_url;
}
// 发送图片 URL 到 Python GLM 服务器进行分析（使用独立上下文）
static void send_url_to_glm_server(const char *image_url)
{
    ESP_LOGI(TAG, "Sending image URL to GLM server...");
    // 每个请求独立的响应上下文
    http_response_ctx_t resp_ctx = {
        .response_buf = NULL,
        .response_len = 0
    };

    // 构建JSON payload
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "image_url", image_url);
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        ESP_LOGE(TAG, "Failed to create JSON payload");
        return;
    }

    // 构建URL
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%s%s", GLM_SERVER_HOST, GLM_SERVER_PORT, GLM_ANALYZE_ENDPOINT);
    ESP_LOGI(TAG, "GLM Server URL: %s", url);

    // HTTP配置
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp_ctx,
        .timeout_ms = 60000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init GLM HTTP client");
        free(json_str);
        return;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_str, strlen(json_str));

    // 执行请求
    ESP_LOGI(TAG, "Sending request to GLM server...");
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);

    free(json_str);
    esp_http_client_cleanup(client);

    // 错误处理
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST to GLM server failed: %s", esp_err_to_name(err));
        bsp_ui_update_status("GLM server error");
        if (resp_ctx.response_buf != NULL) free(resp_ctx.response_buf);
        return;
    }

    ESP_LOGI(TAG, "GLM server response status: %d", status_code);
    if (status_code == 200 && resp_ctx.response_buf != NULL) {
        ESP_LOGI(TAG, "=== GLM Server Response ===");
        ESP_LOGI(TAG, "%s", resp_ctx.response_buf);
        ESP_LOGI(TAG, "===========================");

        // 解析响应
        cJSON *json = cJSON_Parse(resp_ctx.response_buf);
        if (json) {
            cJSON *success = cJSON_GetObjectItem(json, "success");
            if (success && cJSON_IsTrue(success)) {
                cJSON *result = cJSON_GetObjectItem(json, "result");
                if (result && cJSON_IsString(result)) {
                    ESP_LOGI(TAG, "=== AI Analysis Result ===");
                    ESP_LOGI(TAG, "%s", result->valuestring);
                    ESP_LOGI(TAG, "==========================");
                    bsp_ui_update_result(result->valuestring);
                    bsp_ui_update_status("Analysis done");
                }
            } else {
                cJSON *error = cJSON_GetObjectItem(json, "error");
                if (error && cJSON_IsString(error)) {
                    ESP_LOGE(TAG, "GLM server error: %s", error->valuestring);
                    bsp_ui_update_status("Analysis failed");
                }
            }
            cJSON_Delete(json);
        }
    } else {
        ESP_LOGE(TAG, "GLM server returned error: %d", status_code);
        bsp_ui_update_status("Analysis failed");
    }

    // 释放缓冲区
    if (resp_ctx.response_buf != NULL) {
        free(resp_ctx.response_buf);
    }
}
/*
static void call_glm_api(const char *image_url)
{
    ESP_LOGI(TAG, "Calling GLM API with image: %s", image_url);
    bsp_ui_update_status("Recognizing...");

    // 每个请求独立的响应上下文，和其他请求完全隔离
    http_response_ctx_t resp_ctx = {
        .response_buf = NULL,
        .response_len = 0
    };

    // ========== 1. 构建 GLM API 的 JSON payload ==========
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON root");
        bsp_ui_update_status("JSON 创建失败");
        return;
    }

    // 模型名称
    cJSON_AddStringToObject(root, "model", "glm-4v"); // 智谱官方视觉模型名，glm-4.6v需确认是否支持
    // messages数组
    cJSON *messages = cJSON_CreateArray();
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "role", "user");
    // content数组
    cJSON *content = cJSON_CreateArray();

    // 图片URL对象
    cJSON *image_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(image_obj, "type", "image_url");
    cJSON *image_url_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(image_url_obj, "url", image_url);
    cJSON_AddItemToObject(image_obj, "image_url", image_url_obj);
    cJSON_AddItemToArray(content, image_obj);

    // 问题文本对象
    cJSON *text_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(text_obj, "type", "text");
    cJSON_AddStringToObject(text_obj, "text", QUESTION);
    cJSON_AddItemToArray(content, text_obj);

    // 组装JSON层级
    cJSON_AddItemToObject(message, "content", content);
    cJSON_AddItemToArray(messages, message);
    cJSON_AddItemToObject(root, "messages", messages);

    // 注意：智谱API的thinking参数仅支持特定模型，普通模型不需要，这里注释掉，需要的话再打开
    // cJSON *thinking = cJSON_CreateObject();
    // cJSON_AddStringToObject(thinking, "type", "enabled");
    // cJSON_AddItemToObject(root, "thinking", thinking);

    // 生成JSON字符串
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root); // 用完立即释放cJSON，避免内存泄漏
    root = NULL;

    if (!json_str) {
        ESP_LOGE(TAG, "Failed to generate JSON string");
        bsp_ui_update_status("JSON 生成失败");
        return;
    }

    // ========== 2. 配置 HTTPS 客户端 ==========
    esp_http_client_config_t config = {
        .url = GLM_API_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp_ctx, // 关键：把当前上下文传给事件回调
        .timeout_ms = 30000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach, // 使用内置CA证书包
        .buffer_size = 2048, // 调大缓冲区，GLM响应内容较长
        .buffer_size_tx = 2048,
        .disable_auto_redirect = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTPS client");
        free(json_str);
        bsp_ui_update_status("HTTP init failed");
        return;
    }

    // 设置请求头
    char auth_header[512] = {0}; // 扩大缓冲区，避免API_KEY过长溢出
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", API_KEY);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_str, strlen(json_str));

    // ========== 3. 执行 HTTPS 请求 ==========
    ESP_LOGI(TAG, "Performing HTTPS request to GLM API...");
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);

    // 立即释放用完的内存
    free(json_str);
    json_str = NULL;
    esp_http_client_cleanup(client);
    client = NULL;

    // ========== 4. 请求错误处理 ==========
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTPS POST failed: %s", esp_err_to_name(err));
        bsp_ui_update_status("Network error");
        // 无论成功失败，都要释放响应缓冲区
        if (resp_ctx.response_buf != NULL) {
            free(resp_ctx.response_buf);
        }
        return;
    }

    ESP_LOGI(TAG, "GLM API status code: %d", status_code);

    // ========== 5. 解析 GLM API 响应 ==========
    if (status_code == 200 && resp_ctx.response_buf != NULL && resp_ctx.response_len > 0) {
        ESP_LOGI(TAG, "=== GLM API Response ===");
        ESP_LOGI(TAG, "%s", resp_ctx.response_buf);
        ESP_LOGI(TAG, "========================");

        // 解析JSON响应
        cJSON *json = cJSON_Parse(resp_ctx.response_buf);
        if (json == NULL) {
            ESP_LOGE(TAG, "Failed to parse GLM response JSON");
            bsp_ui_update_status("Parse failed");
            free(resp_ctx.response_buf);
            return;
        }

        // 提取AI识别结果
        cJSON *choices = cJSON_GetObjectItem(json, "choices");
        cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItem(first_choice, "message");
        cJSON *content_item = cJSON_GetObjectItem(message, "content");

        if (content_item && cJSON_IsString(content_item)) {
            ESP_LOGI(TAG, "=== AI Analysis Result ===");
            ESP_LOGI(TAG, "%s", content_item->valuestring);
            ESP_LOGI(TAG, "==========================");
            // 更新UI显示结果
            bsp_ui_update_result(content_item->valuestring);
            bsp_ui_update_status("Done");
        } else {
            ESP_LOGE(TAG, "No valid content in GLM response");
            bsp_ui_update_status("No result");
        }

        // 释放cJSON内存
        cJSON_Delete(json);
    } else {
        // 接口返回错误
        ESP_LOGE(TAG, "GLM API failed, status code: %d", status_code);
        if (resp_ctx.response_buf != NULL) {
            ESP_LOGE(TAG, "Error response: %s", resp_ctx.response_buf);
        }
        char status_msg[64];
        snprintf(status_msg, sizeof(status_msg), "API error: %d", status_code);
        bsp_ui_update_status(status_msg);
    }

    // ========== 6. 最终释放响应缓冲区，绝对避免内存泄漏 ==========
    if (resp_ctx.response_buf != NULL) {
        free(resp_ctx.response_buf);
        resp_ctx.response_buf = NULL;
    }
}
*/
// 拍照并上传任务
static void capture_and_upload_task(void *arg)
{
    ESP_LOGI(TAG, "=== Capture and upload task started ===");
    char *image_url = NULL;
    uint8_t *jpeg_buf = NULL;
    camera_fb_t *fb = NULL;
    bool need_free_jpeg = false;

    // ========== 1. 检查WiFi连接 ==========
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        ESP_LOGE(TAG, "WiFi netif not found");
        bsp_ui_update_status("WiFi not init");
        goto exit; // 统一用goto做退出清理，避免内存泄漏
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        ESP_LOGE(TAG, "WiFi not connected!");
        bsp_ui_update_status("WiFi not connected");
        goto exit;
    }
    ESP_LOGI(TAG, "WiFi connected, IP: " IPSTR, IP2STR(&ip_info.ip));

    // ========== 2. 获取摄像头帧 ==========
    bsp_ui_update_status("Capturing...");
    fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Failed to capture image - no frame available");
        bsp_ui_update_status("Capture failed");
        goto exit;
    }

    ESP_LOGI(TAG, "Captured image: %dx%d, format=%d, %zu bytes",
             fb->width, fb->height, fb->format, fb->len);

    // ========== 3. 转换为JPEG格式 ==========
    bsp_ui_update_status("Compressing...");
    if (fb->format == PIXFORMAT_JPEG) {
        // 摄像头已经输出JPEG，直接用
        ESP_LOGI(TAG, "Image is already JPEG format");
        jpeg_buf = fb->buf;
        jpeg_len = fb->len;
    } else {
        // RGB565转JPEG，质量95，平衡画质和体积
        ESP_LOGI(TAG, "Converting RGB565 to JPEG...");
        bool converted = frame2jpg(fb, 95, &jpeg_buf, &jpeg_len);
        if (!converted) {
            ESP_LOGE(TAG, "JPEG conversion failed");
            bsp_ui_update_status("Compress failed");
            goto exit;
        }
        ESP_LOGI(TAG, "JPEG conversion successful: %zu bytes", jpeg_len);
        need_free_jpeg = true;
    }

    // ========== 4. 上传图片到服务器 ==========
    bsp_ui_update_status("Uploading...");
    ESP_LOGI(TAG, "Starting upload to server...");
    image_url = upload_image_to_server(jpeg_buf, jpeg_len);

    if (!image_url) {
        ESP_LOGE(TAG, "Upload failed - no URL returned");
        bsp_ui_update_status("Upload failed");
        goto exit;
    }

    // 上传成功
    ESP_LOGI(TAG, "Upload successful, got image URL");
    bsp_ui_update_status("Upload OK (200)");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Image URL: %s", image_url);
    ESP_LOGI(TAG, "========================================");

    // ========== 5. 调用AI识别 ==========
    bsp_ui_update_status("Analyzing...");
    
    send_url_to_glm_server(image_url); // 用Python服务器
    // call_glm_api(image_url); // 直接ESP32调用GLM API，二选一注释掉

exit:
    // ========== 统一退出清理：所有分支都会执行，绝对避免内存泄漏 ==========
    // 释放JPEG缓冲区
    if (need_free_jpeg && jpeg_buf != NULL) {
        free(jpeg_buf);
    }
    // 释放摄像头帧（使用 esp_camera_fb_return 归还原始帧）
    if (fb != NULL) {
        esp_camera_fb_return(fb);
    }
    // 释放图片URL
    if (image_url != NULL) {
        free(image_url);
    }

    ESP_LOGI(TAG, "=== Capture and upload task finished ===");
    vTaskDelete(NULL);
}
// 拍照按钮回调
static void btn_capture_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "=== Capture button clicked ===");

        bsp_ui_update_status("Starting capture...");

        // 使用普通动态任务创建（6KB 栈）
        BaseType_t ret = xTaskCreatePinnedToCore(
            capture_and_upload_task,
            "capture_upload",
            6144,  // 6KB 栈
            NULL,
            5,
            NULL,
            1  // Core 1
        );

        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create capture task");
            bsp_ui_update_status("Task create failed");
        } else {
            ESP_LOGI(TAG, "Capture task created successfully");
        }
    }
}

// 摄像头显示任务 - 直接使用 esp_camera_fb_get()
static void camera_display_task(void *arg)
{
    ESP_LOGI(TAG, "Camera display task started (Canvas Mode)");

    while (camera_view_active) {
        // 直接获取原始帧，和 bsp_lcd.c 中的方式一样
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb == NULL) {
            ESP_LOGW(TAG, "Failed to get camera frame");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (canvas_buffer != NULL && camera_canvas != NULL) {
            // 直接复制数据，不做字节交换（和 bsp_lcd.c 一样）
            memcpy(canvas_buffer, fb->buf, fb->len);

            // 通知 LVGL 重绘画布
            bsp_display_lock(0);
            lv_obj_invalidate(camera_canvas);
            bsp_display_unlock();
        }

        // 归还原始帧
        esp_camera_fb_return(fb);

        // 增加延迟，避免饿死IDLE任务
        vTaskDelay(pdMS_TO_TICKS(50)); // 约 20fps，给IDLE任务更多时间
    }

    ESP_LOGI(TAG, "Camera display task exiting");
    camera_display_task_handle = NULL;
    vTaskDelete(NULL);
}

// Start 按钮回调 - 进入摄像头查看模式
static void btn_start_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Start button clicked - entering camera view");

        // 检查任务是否已存在
        if (camera_display_task_handle != NULL) {
            ESP_LOGW(TAG, "Camera display task already running");
            return;
        }

        // 分配 canvas 缓冲区（PSRAM）
        if (canvas_buffer == NULL) {
            canvas_buffer = heap_caps_malloc(320 * 240 * 2, MALLOC_CAP_SPIRAM);
            if (canvas_buffer == NULL) {
                ESP_LOGE(TAG, "Failed to allocate canvas buffer");
                return;
            }
            ESP_LOGI(TAG, "Canvas buffer allocated: %d bytes", 320 * 240 * 2);
        }

        camera_view_active = true;

        bsp_display_lock(0);

        // 隐藏主页面的所有控件
        lv_obj_add_flag(btn_capture, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(btn_start, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(label_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(textarea_result, LV_OBJ_FLAG_HIDDEN);

        // 创建 canvas 对象（全屏，作为背景层）
        lv_obj_t *screen = lv_screen_active();
        camera_canvas = lv_canvas_create(screen);
        lv_obj_set_pos(camera_canvas, 0, 0);
        lv_canvas_set_buffer(camera_canvas, canvas_buffer, 320, 240, LV_COLOR_FORMAT_RGB565);
        lv_obj_move_background(camera_canvas);  // 置底

        // 显示 Cancel 按钮（置顶）
        lv_obj_clear_flag(btn_cancel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(btn_cancel);

        bsp_display_unlock();

        // 创建摄像头显示任务
        BaseType_t ret = xTaskCreatePinnedToCore(
            camera_display_task,
            "cam_display",
            4096,
            NULL,
            1,  // 优先级降到1，IDLE任务是0，确保IDLE能运行
            &camera_display_task_handle,
            0  // Core 0
        );

        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create camera display task");
            camera_view_active = false;
        }
    }
}

// Cancel 按钮回调 - 退出摄像头查看模式
static void btn_cancel_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Cancel button clicked - exiting camera view");

        // 停止摄像头显示任务
        camera_view_active = false;

        // 等待任务退出
        vTaskDelay(pdMS_TO_TICKS(100));

        bsp_display_lock(0);

        // 删除 canvas 对象
        if (camera_canvas != NULL) {
            lv_obj_del(camera_canvas);
            camera_canvas = NULL;
        }

        // 隐藏 Cancel 按钮
        lv_obj_add_flag(btn_cancel, LV_OBJ_FLAG_HIDDEN);

        // 显示主页面的控件
        lv_obj_clear_flag(btn_capture, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(btn_start, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(label_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(textarea_result, LV_OBJ_FLAG_HIDDEN);

        bsp_display_unlock();
    }
}

// 初始化 UI
esp_err_t bsp_ui_init(void)
{
    ESP_LOGI(TAG, "Initializing UI");

    bsp_display_lock(0);

    lv_obj_t *screen = lv_screen_active();

    // 清空屏幕上的所有对象
    lv_obj_clean(screen);

    // 设置屏幕背景为黑色
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);

    // ========== 主页面控件 ==========
    // 创建"拍照上传"按钮（右上角）
    btn_capture = lv_button_create(screen);
    lv_obj_set_size(btn_capture, 100, 40);
    lv_obj_align(btn_capture, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_add_event_cb(btn_capture, btn_capture_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label_capture = lv_label_create(btn_capture);
    lv_label_set_text(label_capture, "Capture");
    lv_obj_center(label_capture);

    // 创建"Start"按钮（右上角，拍照按钮下方）
    btn_start = lv_button_create(screen);
    lv_obj_set_size(btn_start, 100, 40);
    lv_obj_align(btn_start, LV_ALIGN_TOP_RIGHT, -10, 60);
    lv_obj_add_event_cb(btn_start, btn_start_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label_start = lv_label_create(btn_start);
    lv_label_set_text(label_start, "Start");
    lv_obj_center(label_start);

    // 创建状态标签（底部）
    label_status = lv_label_create(screen);
    lv_label_set_text(label_status, "Ready");
    lv_obj_set_style_text_color(label_status, lv_color_hex(0x00FF00), 0);
    lv_obj_align(label_status, LV_ALIGN_BOTTOM_LEFT, 10, -10);

    // 创建结果文本框（底部中间，占据大部分宽度）
    textarea_result = lv_textarea_create(screen);
    lv_obj_set_size(textarea_result, 300, 80);
    lv_obj_align(textarea_result, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_textarea_set_text(textarea_result, "图片的主要内容是一个人的脸部局部 (包含头发和佩戴的眼镜). （）。，人物可能在进行自拍。");
    lv_textarea_set_one_line(textarea_result, false);
    lv_obj_set_style_text_font(textarea_result, &lv_font_siyuanbold_jibenhanzi_16, 0);

    // ========== 摄像头查看页面控件 ==========
    // 创建"Cancel"按钮（底部中间，默认隐藏）
    btn_cancel = lv_button_create(screen);
    lv_obj_set_size(btn_cancel, 100, 40);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(btn_cancel, btn_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(btn_cancel, LV_OBJ_FLAG_HIDDEN);  // 默认隐藏

    lv_obj_t *label_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(label_cancel, "Cancel");
    lv_obj_center(label_cancel);

    bsp_display_unlock();

    ESP_LOGI(TAG, "UI initialized successfully");
    return ESP_OK;
}

// 更新识别结果
void bsp_ui_update_result(const char *text)
{
    if (textarea_result && text) {
        bsp_display_lock(0);
        lv_textarea_set_text(textarea_result, text);
        bsp_display_unlock();
    }
}

// 更新状态
void bsp_ui_update_status(const char *status)
{
    if (label_status && status) {
        bsp_display_lock(0);
        lv_label_set_text(label_status, status);
        bsp_display_unlock();
    }
}

