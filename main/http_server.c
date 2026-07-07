#include "http_server.h"

#include "bsp_camera.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "img_converters.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "web_console";

#define PART_BOUNDARY "123456789000000000000987654321"
#define CONFIG_NAMESPACE "webai"

#define DEFAULT_UPLOAD_URL "http://115.190.73.223/xiaozhi/admin/images"
#define DEFAULT_API_URL "https://open.bigmodel.cn/api/paas/v4/chat/completions"
#define DEFAULT_MODEL "glm-4v"
#define DEFAULT_PROMPT "Describe the main content of this image."

#define MAX_URL_LEN 256
#define MAX_API_KEY_LEN 256
#define MAX_MODEL_LEN 64
#define MAX_PROMPT_LEN 384
#define MAX_CONFIG_BODY 2048
#define MAX_HTTP_RESPONSE 24576

typedef struct {
    char upload_url[MAX_URL_LEN];
    char api_url[MAX_URL_LEN];
    char api_key[MAX_API_KEY_LEN];
    char model[MAX_MODEL_LEN];
    char prompt[MAX_PROMPT_LEN];
} web_ai_config_t;

typedef struct {
    char *response_buf;
    size_t response_len;
    size_t max_len;
    bool overflow;
} http_response_ctx_t;

static httpd_handle_t camera_httpd = NULL;
static SemaphoreHandle_t config_mutex = NULL;

static web_ai_config_t current_config = {
    .upload_url = DEFAULT_UPLOAD_URL,
    .api_url = DEFAULT_API_URL,
    .api_key = "",
    .model = DEFAULT_MODEL,
    .prompt = DEFAULT_PROMPT,
};

static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static void safe_copy(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

static char *dup_string(const char *src)
{
    if (src == NULL) {
        return NULL;
    }
    size_t len = strlen(src) + 1;
    char *out = malloc(len);
    if (out != NULL) {
        memcpy(out, src, len);
    }
    return out;
}

static bool starts_with_ci(const char *text, const char *prefix)
{
    if (text == NULL || prefix == NULL) {
        return false;
    }
    while (*prefix != '\0') {
        if (tolower((unsigned char)*text) != tolower((unsigned char)*prefix)) {
            return false;
        }
        text++;
        prefix++;
    }
    return true;
}

static bool is_https_url(const char *url)
{
    return starts_with_ci(url, "https://");
}

static void config_mutex_init(void)
{
    if (config_mutex == NULL) {
        config_mutex = xSemaphoreCreateMutex();
    }
}

static void config_copy(web_ai_config_t *out)
{
    config_mutex_init();
    if (config_mutex != NULL) {
        xSemaphoreTake(config_mutex, portMAX_DELAY);
    }
    *out = current_config;
    if (config_mutex != NULL) {
        xSemaphoreGive(config_mutex);
    }
}

static void nvs_get_string_or_keep(nvs_handle_t nvs, const char *key, char *dst, size_t dst_size)
{
    size_t len = dst_size;
    esp_err_t err = nvs_get_str(nvs, key, dst, &len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS read %s failed: %s", key, esp_err_to_name(err));
    }
}

static void config_load(void)
{
    config_mutex_init();

    web_ai_config_t loaded = current_config;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        nvs_get_string_or_keep(nvs, "uploadUrl", loaded.upload_url, sizeof(loaded.upload_url));
        nvs_get_string_or_keep(nvs, "apiUrl", loaded.api_url, sizeof(loaded.api_url));
        nvs_get_string_or_keep(nvs, "apiKey", loaded.api_key, sizeof(loaded.api_key));
        nvs_get_string_or_keep(nvs, "model", loaded.model, sizeof(loaded.model));
        nvs_get_string_or_keep(nvs, "prompt", loaded.prompt, sizeof(loaded.prompt));
        nvs_close(nvs);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(err));
    }

    if (config_mutex != NULL) {
        xSemaphoreTake(config_mutex, portMAX_DELAY);
    }
    current_config = loaded;
    if (config_mutex != NULL) {
        xSemaphoreGive(config_mutex);
    }
}

static esp_err_t config_save(const web_ai_config_t *cfg)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open for save failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(nvs, "uploadUrl", cfg->upload_url);
    if (err == ESP_OK) err = nvs_set_str(nvs, "apiUrl", cfg->api_url);
    if (err == ESP_OK) err = nvs_set_str(nvs, "apiKey", cfg->api_key);
    if (err == ESP_OK) err = nvs_set_str(nvs, "model", cfg->model);
    if (err == ESP_OK) err = nvs_set_str(nvs, "prompt", cfg->prompt);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);

    if (err == ESP_OK) {
        if (config_mutex != NULL) {
            xSemaphoreTake(config_mutex, portMAX_DELAY);
        }
        current_config = *cfg;
        if (config_mutex != NULL) {
            xSemaphoreGive(config_mutex);
        }
    }
    return err;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode_inplace(char *text)
{
    char *src = text;
    char *dst = text;

    while (*src != '\0') {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            int high = hex_value(src[1]);
            int low = hex_value(src[2]);
            *dst++ = (char)((high << 4) | low);
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static bool form_get_value(const char *body, const char *key, char *out, size_t out_size)
{
    size_t key_len = strlen(key);
    const char *p = body;

    if (out_size == 0) {
        return false;
    }
    out[0] = '\0';

    while (p != NULL && *p != '\0') {
        const char *next = strchr(p, '&');
        const char *end = next != NULL ? next : p + strlen(p);
        const char *eq = memchr(p, '=', (size_t)(end - p));

        if (eq != NULL && (size_t)(eq - p) == key_len && strncmp(p, key, key_len) == 0) {
            size_t value_len = (size_t)(end - eq - 1);
            if (value_len >= out_size) {
                value_len = out_size - 1;
            }
            memcpy(out, eq + 1, value_len);
            out[value_len] = '\0';
            url_decode_inplace(out);
            return true;
        }

        p = next != NULL ? next + 1 : NULL;
    }

    return false;
}

static esp_err_t read_request_body(httpd_req_t *req, char **out)
{
    if (req->content_len > MAX_CONFIG_BODY) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"body too large\"}");
        return ESP_FAIL;
    }

    char *body = calloc(req->content_len + 1, 1);
    if (body == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            }
            free(body);
            return ESP_FAIL;
        }
        received += (size_t)ret;
    }

    body[received] = '\0';
    *out = body;
    return ESP_OK;
}

static esp_err_t response_append(http_response_ctx_t *ctx, const char *data, size_t len)
{
    if (ctx == NULL || data == NULL || len == 0) {
        return ESP_OK;
    }

    size_t allowed = len;
    if (ctx->max_len > 0 && ctx->response_len + allowed > ctx->max_len) {
        allowed = ctx->max_len - ctx->response_len;
        ctx->overflow = true;
    }

    if (allowed == 0) {
        return ESP_OK;
    }

    char *new_buf = realloc(ctx->response_buf, ctx->response_len + allowed + 1);
    if (new_buf == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ctx->response_buf = new_buf;
    memcpy(ctx->response_buf + ctx->response_len, data, allowed);
    ctx->response_len += allowed;
    ctx->response_buf[ctx->response_len] = '\0';
    return ESP_OK;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_ctx_t *ctx = (http_response_ctx_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx != NULL && evt->data_len > 0) {
        return response_append(ctx, (const char *)evt->data, (size_t)evt->data_len);
    }
    return ESP_OK;
}

static esp_err_t capture_jpeg(uint8_t **jpeg_buf, size_t *jpeg_len)
{
    *jpeg_buf = NULL;
    *jpeg_len = 0;

    if (bsp_camera_lock(1000) != ESP_OK) {
        ESP_LOGW(TAG, "Camera lock timeout");
        return ESP_ERR_TIMEOUT;
    }
    camera_fb_t *fb = esp_camera_fb_get();
    bsp_camera_unlock();

    if (fb == NULL) {
        ESP_LOGE(TAG, "Camera frame is NULL");
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;
    if (fb->format == PIXFORMAT_JPEG) {
        uint8_t *copy = heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (copy == NULL) {
            copy = malloc(fb->len);
        }
        if (copy == NULL) {
            ret = ESP_ERR_NO_MEM;
        } else {
            memcpy(copy, fb->buf, fb->len);
            *jpeg_buf = copy;
            *jpeg_len = fb->len;
        }
    } else if (!frame2jpg(fb, 80, jpeg_buf, jpeg_len)) {
        ESP_LOGE(TAG, "RGB frame to JPEG failed");
        ret = ESP_FAIL;
    }

    if (bsp_camera_lock(1000) == ESP_OK) {
        esp_camera_fb_return(fb);
        bsp_camera_unlock();
    } else {
        ESP_LOGW(TAG, "Camera return lock timeout");
    }

    return ret;
}

static char *build_absolute_url(const char *base_url, const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return NULL;
    }
    if (starts_with_ci(value, "http://") || starts_with_ci(value, "https://")) {
        return dup_string(value);
    }

    const char *scheme = strstr(base_url, "://");
    if (scheme == NULL) {
        return dup_string(value);
    }

    const char *host_start = scheme + 3;
    const char *path_start = strchr(host_start, '/');
    size_t origin_len = path_start != NULL ? (size_t)(path_start - base_url) : strlen(base_url);

    if (value[0] == '/') {
        size_t len = origin_len + strlen(value) + 1;
        char *out = malloc(len);
        if (out != NULL) {
            snprintf(out, len, "%.*s%s", (int)origin_len, base_url, value);
        }
        return out;
    }

    const char *last_slash = strrchr(base_url, '/');
    size_t prefix_len = (last_slash != NULL && last_slash > host_start) ? (size_t)(last_slash - base_url + 1) : strlen(base_url);
    size_t len = prefix_len + strlen(value) + 1;
    char *out = malloc(len);
    if (out != NULL) {
        snprintf(out, len, "%.*s%s", (int)prefix_len, base_url, value);
    }
    return out;
}

static char *extract_upload_url(const char *upload_url, const char *response)
{
    if (response == NULL || response[0] == '\0') {
        return NULL;
    }

    if (starts_with_ci(response, "http://") || starts_with_ci(response, "https://")) {
        return dup_string(response);
    }

    char *result = NULL;
    cJSON *root = cJSON_Parse(response);
    if (root != NULL) {
        cJSON *url_item = cJSON_GetObjectItem(root, "url");
        if (!cJSON_IsString(url_item)) {
            url_item = cJSON_GetObjectItem(root, "image_url");
        }
        if (!cJSON_IsString(url_item)) {
            cJSON *data = cJSON_GetObjectItem(root, "data");
            if (data != NULL) {
                url_item = cJSON_GetObjectItem(data, "url");
                if (!cJSON_IsString(url_item)) {
                    url_item = cJSON_GetObjectItem(data, "image_url");
                }
            }
        }

        if (cJSON_IsString(url_item)) {
            result = build_absolute_url(upload_url, url_item->valuestring);
        }
        cJSON_Delete(root);
    }

    return result;
}

static char *upload_image(const web_ai_config_t *cfg, const uint8_t *image_data, size_t image_len)
{
    const char *boundary = "----esp32-web-console-image";
    char header[256];
    char footer[64];
    char content_type[96];
    char chunk[512];
    http_response_ctx_t resp = {
        .response_buf = NULL,
        .response_len = 0,
        .max_len = 8192,
        .overflow = false,
    };

    int header_len = snprintf(header, sizeof(header),
                              "--%s\r\n"
                              "Content-Disposition: form-data; name=\"file\"; filename=\"capture.jpg\"\r\n"
                              "Content-Type: image/jpeg\r\n\r\n",
                              boundary);
    int footer_len = snprintf(footer, sizeof(footer), "\r\n--%s--\r\n", boundary);
    if (header_len < 0 || header_len >= (int)sizeof(header) ||
        footer_len < 0 || footer_len >= (int)sizeof(footer)) {
        return NULL;
    }

    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);
    size_t total_len = (size_t)header_len + image_len + (size_t)footer_len;

    esp_http_client_config_t http_cfg = {
        .url = cfg->upload_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
        .crt_bundle_attach = is_https_url(cfg->upload_url) ? esp_crt_bundle_attach : NULL,
        .disable_auto_redirect = false,
    };

    ESP_LOGI(TAG, "Uploading image to %s, len=%u", cfg->upload_url, (unsigned)image_len);
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (client == NULL) {
        return NULL;
    }

    esp_http_client_set_header(client, "Content-Type", content_type);

    esp_err_t err = esp_http_client_open(client, (int)total_len);
    if (err == ESP_OK) {
        const char *parts[] = {
            header,
            (const char *)image_data,
            footer,
        };
        size_t lengths[] = {
            (size_t)header_len,
            image_len,
            (size_t)footer_len,
        };

        for (size_t i = 0; i < 3 && err == ESP_OK; i++) {
            size_t sent = 0;
            while (sent < lengths[i]) {
                int written = esp_http_client_write(client, parts[i] + sent, lengths[i] - sent);
                if (written <= 0) {
                    err = ESP_FAIL;
                    break;
                }
                sent += (size_t)written;
            }
        }
    }

    int status = 0;
    if (err == ESP_OK) {
        (void)esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        while (true) {
            int read_len = esp_http_client_read(client, chunk, sizeof(chunk));
            if (read_len <= 0) {
                break;
            }
            if (response_append(&resp, chunk, (size_t)read_len) != ESP_OK) {
                err = ESP_ERR_NO_MEM;
                break;
            }
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGE(TAG, "Image upload failed: err=%s status=%d resp=%s",
                 esp_err_to_name(err), status, resp.response_buf ? resp.response_buf : "");
        free(resp.response_buf);
        return NULL;
    }

    ESP_LOGI(TAG, "Image upload response: %s", resp.response_buf ? resp.response_buf : "");
    char *image_url = extract_upload_url(cfg->upload_url, resp.response_buf);
    free(resp.response_buf);
    return image_url;
}

static cJSON *build_model_payload(const web_ai_config_t *cfg, const char *image_url)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *messages = cJSON_CreateArray();
    cJSON *message = cJSON_CreateObject();
    cJSON *content = cJSON_CreateArray();
    cJSON *image_obj = cJSON_CreateObject();
    cJSON *image_url_obj = cJSON_CreateObject();
    cJSON *text_obj = cJSON_CreateObject();

    if (!root || !messages || !message || !content || !image_obj || !image_url_obj || !text_obj) {
        cJSON_Delete(root);
        cJSON_Delete(messages);
        cJSON_Delete(message);
        cJSON_Delete(content);
        cJSON_Delete(image_obj);
        cJSON_Delete(image_url_obj);
        cJSON_Delete(text_obj);
        return NULL;
    }

    cJSON_AddStringToObject(root, "model", cfg->model);
    cJSON_AddStringToObject(message, "role", "user");

    cJSON_AddStringToObject(image_obj, "type", "image_url");
    cJSON_AddStringToObject(image_url_obj, "url", image_url);
    cJSON_AddItemToObject(image_obj, "image_url", image_url_obj);
    cJSON_AddItemToArray(content, image_obj);

    cJSON_AddStringToObject(text_obj, "type", "text");
    cJSON_AddStringToObject(text_obj, "text", cfg->prompt);
    cJSON_AddItemToArray(content, text_obj);

    cJSON_AddItemToObject(message, "content", content);
    cJSON_AddItemToArray(messages, message);
    cJSON_AddItemToObject(root, "messages", messages);
    return root;
}

static char *extract_model_text(const char *response)
{
    if (response == NULL || response[0] == '\0') {
        return dup_string("");
    }

    char *result = NULL;
    cJSON *root = cJSON_Parse(response);
    if (root != NULL) {
        cJSON *choices = cJSON_GetObjectItem(root, "choices");
        cJSON *first = cJSON_GetArrayItem(choices, 0);
        cJSON *message = first ? cJSON_GetObjectItem(first, "message") : NULL;
        cJSON *content = message ? cJSON_GetObjectItem(message, "content") : NULL;
        if (cJSON_IsString(content)) {
            result = dup_string(content->valuestring);
        }

        if (result == NULL) {
            cJSON *direct = cJSON_GetObjectItem(root, "result");
            if (cJSON_IsString(direct)) {
                result = dup_string(direct->valuestring);
            }
        }
        if (result == NULL) {
            cJSON *direct = cJSON_GetObjectItem(root, "output_text");
            if (cJSON_IsString(direct)) {
                result = dup_string(direct->valuestring);
            }
        }
        cJSON_Delete(root);
    }

    return result != NULL ? result : dup_string(response);
}

static char *call_cloud_model(const web_ai_config_t *cfg, const char *image_url, char **raw_response_out)
{
    *raw_response_out = NULL;

    cJSON *payload = build_model_payload(cfg, image_url);
    if (payload == NULL) {
        return NULL;
    }
    char *json_str = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    if (json_str == NULL) {
        return NULL;
    }

    http_response_ctx_t resp = {
        .response_buf = NULL,
        .response_len = 0,
        .max_len = MAX_HTTP_RESPONSE,
        .overflow = false,
    };

    esp_http_client_config_t http_cfg = {
        .url = cfg->api_url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 60000,
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
        .crt_bundle_attach = is_https_url(cfg->api_url) ? esp_crt_bundle_attach : NULL,
        .disable_auto_redirect = false,
    };

    ESP_LOGI(TAG, "Calling cloud model API: %s", cfg->api_url);
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (client == NULL) {
        cJSON_free(json_str);
        return NULL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (cfg->api_key[0] != '\0') {
        char auth_header[MAX_API_KEY_LEN + 16];
        if (starts_with_ci(cfg->api_key, "Bearer ")) {
            safe_copy(auth_header, sizeof(auth_header), cfg->api_key);
        } else {
            snprintf(auth_header, sizeof(auth_header), "Bearer %s", cfg->api_key);
        }
        esp_http_client_set_header(client, "Authorization", auth_header);
    }
    esp_http_client_set_post_field(client, json_str, strlen(json_str));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    cJSON_free(json_str);

    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGE(TAG, "Cloud model API failed: err=%s status=%d resp=%s",
                 esp_err_to_name(err), status, resp.response_buf ? resp.response_buf : "");
        *raw_response_out = resp.response_buf;
        return NULL;
    }

    if (resp.overflow) {
        ESP_LOGW(TAG, "Cloud model response truncated at %u bytes", (unsigned)resp.max_len);
    }

    char *text = extract_model_text(resp.response_buf);
    *raw_response_out = resp.response_buf;
    return text;
}

static esp_err_t send_json_config(httpd_req_t *req)
{
    web_ai_config_t cfg;
    config_copy(&cfg);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON_AddStringToObject(root, "uploadUrl", cfg.upload_url);
    cJSON_AddStringToObject(root, "apiUrl", cfg.api_url);
    cJSON_AddBoolToObject(root, "apiKeySet", cfg.api_key[0] != '\0');
    cJSON_AddStringToObject(root, "model", cfg.model);
    cJSON_AddStringToObject(root, "prompt", cfg.prompt);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json, strlen(json));
    cJSON_free(json);
    return ret;
}

static esp_err_t send_error_json(httpd_req_t *req, const char *status, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    cJSON_AddBoolToObject(root, "success", false);
    cJSON_AddStringToObject(root, "error", message);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json, strlen(json));
    cJSON_free(json);
    return ret;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    static const char html[] =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP32 Image Console</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;margin:0;background:#f6f7f9;color:#1f2933}"
        "main{max-width:760px;margin:0 auto;padding:16px}"
        "h1{font-size:22px;margin:8px 0 14px}"
        ".panel{background:#fff;border:1px solid #d8dde6;border-radius:8px;padding:14px;margin-bottom:12px}"
        "label{display:block;font-size:13px;font-weight:600;margin:10px 0 5px}"
        "input,textarea{width:100%;box-sizing:border-box;border:1px solid #b8c0cc;border-radius:6px;padding:9px;font:14px Arial,sans-serif}"
        "textarea{min-height:72px;resize:vertical}"
        "button{border:0;border-radius:6px;padding:10px 13px;margin:8px 8px 0 0;background:#1769e0;color:white;font-weight:700}"
        "button.secondary{background:#4b5563}"
        "button:disabled{opacity:.55}"
        "img{display:block;width:100%;max-width:640px;background:#111;border:1px solid #c4ccd8;border-radius:6px}"
        "pre{white-space:pre-wrap;word-break:break-word;background:#0f172a;color:#e5e7eb;border-radius:6px;padding:10px;min-height:80px}"
        ".row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}"
        ".hint{font-size:12px;color:#5c6675}"
        "</style></head><body><main>"
        "<h1>ESP32 Image Console</h1>"
        "<section class='panel'>"
        "<div class='row'>"
        "<button id='shotBtn' onclick='refreshImage()'>Capture</button>"
        "<button id='analyzeBtn' onclick='analyzeImage()'>Analyze</button>"
        "<a class='hint' href='/stream' target='_blank'>MJPEG stream</a>"
        "</div>"
        "<p id='status' class='hint'>Ready</p>"
        "<img id='photo' src='/capture.jpg' alt='camera frame'>"
        "</section>"
        "<section class='panel'>"
        "<label>Image upload URL</label><input id='uploadUrl'>"
        "<label>Cloud model API URL</label><input id='apiUrl'>"
        "<label>API key</label><input id='apiKey' type='password' placeholder='leave blank to keep current key'>"
        "<label>Model</label><input id='model'>"
        "<label>Prompt</label><textarea id='prompt'></textarea>"
        "<button class='secondary' onclick='saveConfig()'>Save config</button>"
        "<span id='keyState' class='hint'></span>"
        "</section>"
        "<section class='panel'><label>Result</label><pre id='result'></pre></section>"
        "</main><script>"
        "const $=id=>document.getElementById(id);"
        "function setStatus(t){$('status').textContent=t;}"
        "async function loadConfig(){"
        "let r=await fetch('/api/config');let c=await r.json();"
        "$('uploadUrl').value=c.uploadUrl||'';$('apiUrl').value=c.apiUrl||'';$('model').value=c.model||'';$('prompt').value=c.prompt||'';"
        "$('keyState').textContent=c.apiKeySet?'API key saved':'API key is empty';"
        "}"
        "async function saveConfig(){"
        "setStatus('Saving config...');"
        "let b=new URLSearchParams();"
        "['uploadUrl','apiUrl','apiKey','model','prompt'].forEach(k=>b.set(k,$(k).value));"
        "let r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b.toString()});"
        "let j=await r.json();if(!r.ok||j.success===false)throw new Error(j.error||'save failed');"
        "$('apiKey').value='';await loadConfig();setStatus('Config saved');"
        "}"
        "function refreshImage(){setStatus('Capturing...');$('photo').src='/capture.jpg?t='+Date.now();$('photo').onload=()=>setStatus('Frame updated');$('photo').onerror=()=>setStatus('Capture failed');}"
        "async function analyzeImage(){"
        "try{$('analyzeBtn').disabled=true;setStatus('Analyzing...');$('result').textContent='';"
        "let r=await fetch('/api/analyze',{method:'POST'});let j=await r.json();"
        "if(!r.ok||j.success===false)throw new Error(j.error||'analyze failed');"
        "$('result').textContent=j.result||j.raw||'';setStatus('Done');refreshImage();}"
        "catch(e){$('result').textContent=String(e);setStatus('Failed');}"
        "finally{$('analyzeBtn').disabled=false;}"
        "}"
        "loadConfig().catch(e=>setStatus(String(e)));"
        "</script></body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, html, sizeof(html) - 1);
}

static esp_err_t capture_handler(httpd_req_t *req)
{
    uint8_t *jpeg_buf = NULL;
    size_t jpeg_len = 0;
    esp_err_t ret = capture_jpeg(&jpeg_buf, &jpeg_len);
    if (ret != ESP_OK) {
        return send_error_json(req, "503 Service Unavailable", "camera capture failed");
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    ret = httpd_resp_send(req, (const char *)jpeg_buf, jpeg_len);
    free(jpeg_buf);
    return ret;
}

static esp_err_t config_get_handler(httpd_req_t *req)
{
    return send_json_config(req);
}

static esp_err_t config_post_handler(httpd_req_t *req)
{
    char *body = NULL;
    if (read_request_body(req, &body) != ESP_OK) {
        return ESP_FAIL;
    }

    web_ai_config_t cfg;
    config_copy(&cfg);

    char value[MAX_PROMPT_LEN];
    if (form_get_value(body, "uploadUrl", value, sizeof(value)) && value[0] != '\0') {
        safe_copy(cfg.upload_url, sizeof(cfg.upload_url), value);
    }
    if (form_get_value(body, "apiUrl", value, sizeof(value)) && value[0] != '\0') {
        safe_copy(cfg.api_url, sizeof(cfg.api_url), value);
    }
    if (form_get_value(body, "apiKey", value, sizeof(value)) && value[0] != '\0') {
        safe_copy(cfg.api_key, sizeof(cfg.api_key), value);
    }
    if (form_get_value(body, "model", value, sizeof(value)) && value[0] != '\0') {
        safe_copy(cfg.model, sizeof(cfg.model), value);
    }
    if (form_get_value(body, "prompt", value, sizeof(value)) && value[0] != '\0') {
        safe_copy(cfg.prompt, sizeof(cfg.prompt), value);
    }
    free(body);

    if (!starts_with_ci(cfg.upload_url, "http://") && !starts_with_ci(cfg.upload_url, "https://")) {
        return send_error_json(req, "400 Bad Request", "uploadUrl must start with http:// or https://");
    }
    if (!starts_with_ci(cfg.api_url, "http://") && !starts_with_ci(cfg.api_url, "https://")) {
        return send_error_json(req, "400 Bad Request", "apiUrl must start with http:// or https://");
    }

    esp_err_t ret = config_save(&cfg);
    if (ret != ESP_OK) {
        return send_error_json(req, "500 Internal Server Error", "config save failed");
    }

    return send_json_config(req);
}

static esp_err_t analyze_handler(httpd_req_t *req)
{
    web_ai_config_t cfg;
    config_copy(&cfg);

    if (cfg.api_key[0] == '\0') {
        return send_error_json(req, "400 Bad Request", "API key is empty");
    }

    uint8_t *jpeg_buf = NULL;
    size_t jpeg_len = 0;
    esp_err_t ret = capture_jpeg(&jpeg_buf, &jpeg_len);
    if (ret != ESP_OK) {
        return send_error_json(req, "503 Service Unavailable", "camera capture failed");
    }

    char *image_url = upload_image(&cfg, jpeg_buf, jpeg_len);
    free(jpeg_buf);
    if (image_url == NULL) {
        return send_error_json(req, "502 Bad Gateway", "image upload failed");
    }

    char *raw_response = NULL;
    char *result = call_cloud_model(&cfg, image_url, &raw_response);
    if (result == NULL) {
        free(image_url);
        free(raw_response);
        return send_error_json(req, "502 Bad Gateway", "cloud model API failed");
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        free(image_url);
        free(raw_response);
        free(result);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "image_url", image_url);
    cJSON_AddStringToObject(root, "result", result);
    if (raw_response != NULL) {
        cJSON_AddStringToObject(root, "raw", raw_response);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(image_url);
    free(raw_response);
    free(result);

    if (json == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    ret = httpd_resp_send(req, json, strlen(json));
    cJSON_free(json);
    return ret;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        return res;
    }

    char part_buf[64];
    while (true) {
        uint8_t *jpeg_buf = NULL;
        size_t jpeg_len = 0;
        res = capture_jpeg(&jpeg_buf, &jpeg_len);
        if (res != ESP_OK) {
            break;
        }

        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        }
        if (res == ESP_OK) {
            size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, (unsigned)jpeg_len);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)jpeg_buf, jpeg_len);
        }
        free(jpeg_buf);

        if (res != ESP_OK) {
            ESP_LOGI(TAG, "Stream client disconnected");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    return res;
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t http_server_start(void)
{
    if (camera_httpd != NULL) {
        ESP_LOGI(TAG, "HTTP server already started");
        return ESP_OK;
    }

    config_load();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_open_sockets = 4;
    config.max_uri_handlers = 8;
    config.recv_wait_timeout = 30;
    config.send_wait_timeout = 30;
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    config.task_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
    };
    httpd_uri_t capture_uri = {
        .uri = "/capture.jpg",
        .method = HTTP_GET,
        .handler = capture_handler,
    };
    httpd_uri_t config_get_uri = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = config_get_handler,
    };
    httpd_uri_t config_post_uri = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = config_post_handler,
    };
    httpd_uri_t analyze_uri = {
        .uri = "/api/analyze",
        .method = HTTP_POST,
        .handler = analyze_handler,
    };
    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
    };
    httpd_uri_t favicon_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_handler,
    };

    ESP_LOGI(TAG, "Starting web console on port %d", config.server_port);
    esp_err_t ret = httpd_start(&camera_httpd, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
        camera_httpd = NULL;
        return ret;
    }

    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    httpd_register_uri_handler(camera_httpd, &config_get_uri);
    httpd_register_uri_handler(camera_httpd, &config_post_uri);
    httpd_register_uri_handler(camera_httpd, &analyze_uri);
    httpd_register_uri_handler(camera_httpd, &stream_uri);
    httpd_register_uri_handler(camera_httpd, &favicon_uri);

    ESP_LOGI(TAG, "Web console ready");
    return ESP_OK;
}

void http_server_stop(void)
{
    if (camera_httpd != NULL) {
        httpd_stop(camera_httpd);
        camera_httpd = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
}
