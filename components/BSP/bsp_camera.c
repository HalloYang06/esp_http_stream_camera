#include "bsp_camera.h"
#include "bsp_i2cbus.h"
#ifdef BSP_CAMERA_USE_PCA9557_PWDN
#include "bsp_pca9557.h"
#endif
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include <string.h>
static const char *TAG = "bsp_camera";

// Camera configuration
static camera_config_t camera_config = {
    .pin_pwdn  = BSP_CAMERA_PIN_PWDN,
    .pin_reset = BSP_CAMERA_PIN_RESET,
    .pin_xclk = BSP_CAMERA_PIN_XCLK,
    .pin_sccb_sda = -1,  // Use I2C bus
    .pin_sccb_scl = -1,

    .pin_d7 = BSP_CAMERA_PIN_D7,
    .pin_d6 = BSP_CAMERA_PIN_D6,
    .pin_d5 = BSP_CAMERA_PIN_D5,
    .pin_d4 = BSP_CAMERA_PIN_D4,
    .pin_d3 = BSP_CAMERA_PIN_D3,
    .pin_d2 = BSP_CAMERA_PIN_D2,
    .pin_d1 = BSP_CAMERA_PIN_D1,
    .pin_d0 = BSP_CAMERA_PIN_D0,
    .pin_vsync = BSP_CAMERA_PIN_VSYNC,
    .pin_href = BSP_CAMERA_PIN_HREF,
    .pin_pclk = BSP_CAMERA_PIN_PCLK,

    .xclk_freq_hz = BSP_CAMERA_XCLK_FREQ_HZ,
    .ledc_timer = LEDC_TIMER_1,     // Use TIMER_1 to avoid conflict with LCD backlight
    .ledc_channel = LEDC_CHANNEL_1,

    .pixel_format = BSP_CAMERA_PIXEL_FORMAT,
    .frame_size = BSP_CAMERA_FRAME_SIZE,
    .jpeg_quality = BSP_CAMERA_JPEG_QUALITY,
    .fb_count = BSP_CAMERA_FB_COUNT,
    .fb_location = BSP_CAMERA_FB_LOCATION,
    .grab_mode = BSP_CAMERA_GRAB_MODE,

    .sccb_i2c_port = BSP_I2C_PORT,
};

esp_err_t bsp_camera_init(void)
{
    esp_err_t ret;

    // Initialize camera power control pin if using direct GPIO
#ifndef BSP_CAMERA_USE_PCA9557_PWDN
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BSP_CAMERA_PWDN_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
#endif

    // Power on camera
    ESP_LOGI(TAG, "Powering on camera");
    BSP_CAMERA_POWER_ON();
    vTaskDelay(pdMS_TO_TICKS(100)); // Wait for camera to stabilize (min 50ms)

    // Initialize camera driver
    ESP_LOGI(TAG, "Initializing camera driver");
    ret = esp_camera_init(&camera_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    

    ESP_LOGI(TAG, "Camera initialized successfully");
    return ESP_OK;
}

esp_err_t bsp_camera_capture(void)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        return ESP_FAIL;
    }

    // Process frame here if needed

    esp_camera_fb_return(fb);
    return ESP_OK;
}

/****************   Camera Task System   *************************************/
// Shared frame buffer for task system
static camera_fb_t *g_latest_fb = NULL;
static SemaphoreHandle_t g_fb_mutex = NULL;
static SemaphoreHandle_t g_fb_ready_sem = NULL;

// Camera capture task
static void camera_capture_task(void *arg)
{
    ESP_LOGI(TAG, "Camera capture task started on Core %d", xPortGetCoreID());
    const TickType_t frame_delay = pdMS_TO_TICKS(50); // ~20fps

    while (1) {
        camera_fb_t *new_fb = esp_camera_fb_get();
        if (new_fb == NULL) {
            ESP_LOGE(TAG, "Camera capture failed");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Lock and update shared frame
        xSemaphoreTake(g_fb_mutex, portMAX_DELAY);

        if (g_latest_fb != NULL) {
            esp_camera_fb_return(g_latest_fb);
        }

        g_latest_fb = new_fb;
        xSemaphoreGive(g_fb_mutex);

        // Signal new frame available
        xSemaphoreGive(g_fb_ready_sem);

        vTaskDelay(frame_delay);
    }

    vTaskDelete(NULL);
}

esp_err_t bsp_camera_tasks_init(void)
{
    ESP_LOGI(TAG, "Initializing camera task system");


    // Create mutex
    g_fb_mutex = xSemaphoreCreateMutex();
    if (g_fb_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create frame mutex");
        return ESP_FAIL;
    }

    // Create counting semaphore (max 10 consumers, initial 0)
    g_fb_ready_sem = xSemaphoreCreateCounting(10, 0);
    if (g_fb_ready_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create frame ready semaphore");
        vSemaphoreDelete(g_fb_mutex);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Frame mutex and semaphore created successfully");

    // 使用 PSRAM 分配任务栈（内部 RAM 不足）
    ESP_LOGI(TAG, "Creating camera capture task with PSRAM stack...");

    // 创建任务配置
    TaskHandle_t task_handle = NULL;
    StaticTask_t *task_buffer = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
    StackType_t *stack_buffer = (StackType_t *)heap_caps_malloc(16384, MALLOC_CAP_SPIRAM);

    if (task_buffer == NULL || stack_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate task buffers");
        if (task_buffer) free(task_buffer);
        if (stack_buffer) free(stack_buffer);
        vSemaphoreDelete(g_fb_mutex);
        vSemaphoreDelete(g_fb_ready_sem);
        return ESP_FAIL;
    }

    task_handle = xTaskCreateStaticPinnedToCore(
        camera_capture_task,
        "cam_capture",
        16384,
        NULL,
        4,
        stack_buffer,
        task_buffer,
        1  // Core 1
    );

    if (task_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create camera capture task");
        free(task_buffer);
        free(stack_buffer);
        vSemaphoreDelete(g_fb_mutex);
        vSemaphoreDelete(g_fb_ready_sem);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Camera task system initialized successfully");
    return ESP_OK;
}

camera_fb_t* bsp_camera_get_frame(TickType_t timeout_ms)
{
    if (xSemaphoreTake(g_fb_ready_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return NULL;
    }

    camera_fb_t *fb_copy = NULL;

    xSemaphoreTake(g_fb_mutex, portMAX_DELAY);

    if (g_latest_fb != NULL) {
        fb_copy = (camera_fb_t *)malloc(sizeof(camera_fb_t));
        if (fb_copy != NULL) {
            memcpy(fb_copy, g_latest_fb, sizeof(camera_fb_t));

            fb_copy->buf = (uint8_t *)malloc(g_latest_fb->len);
            if (fb_copy->buf != NULL) {
                memcpy(fb_copy->buf, g_latest_fb->buf, g_latest_fb->len);
            } else {
                ESP_LOGE(TAG, "Failed to allocate frame buffer");
                free(fb_copy);
                fb_copy = NULL;
            }
        } else {
            ESP_LOGE(TAG, "Failed to allocate frame copy");
        }
    }

    xSemaphoreGive(g_fb_mutex);

    return fb_copy;
}

void bsp_camera_frame_free(camera_fb_t *fb)
{
    if (fb != NULL) {
        if (fb->buf != NULL) {
            free(fb->buf);
        }
        free(fb);
    }
}

/****************   LCD Display Task   ***************************************/
#include "bsp_lcd.h"

static TaskHandle_t lcd_task_handle = NULL;
static bool lcd_display_running = false;

// LCD 显示任务
static void lcd_display_task(void *arg)
{
    ESP_LOGI(TAG, "LCD display task started on Core %d", xPortGetCoreID());

    while (1) {
        // 等待新帧
        camera_fb_t *fb = bsp_camera_get_frame(1000);
        if (fb == NULL) {
            ESP_LOGW(TAG, "Failed to get frame for LCD display");
            continue;
        }

        // 检查格式
        if (fb->format != PIXFORMAT_RGB565) {
            ESP_LOGE(TAG, "Unsupported pixel format for LCD: %d", fb->format);
            bsp_camera_frame_free(fb);
            continue;
        }

        // 分块绘制到 LCD（每次 10 行）
        const int rows_per_chunk = 10;
        const int width = fb->width;
        const int height = fb->height;
        const int bytes_per_pixel = 2; // RGB565

        // 只锁定 LCD，不锁定 LVGL
        for (int y = 0; y < height; y += rows_per_chunk) {
            int chunk_height = (y + rows_per_chunk > height) ? (height - y) : rows_per_chunk;
            size_t offset = y * width * bytes_per_pixel;

            lcd_draw_bitmap(0, y, width, y + chunk_height, fb->buf + offset);
        }

        // 释放帧
        bsp_camera_frame_free(fb);
    }

    vTaskDelete(NULL);
}

// 启动 LCD 显示任务
esp_err_t bsp_camera_lcd_task_start(void)
{
    if (lcd_task_handle != NULL) {
        ESP_LOGW(TAG, "LCD display task already running");
        return ESP_OK;
    }

    // 使用 PSRAM 分配任务栈
    ESP_LOGI(TAG, "Creating LCD display task with PSRAM stack...");

    StaticTask_t *task_buffer = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
    StackType_t *stack_buffer = (StackType_t *)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);

    if (task_buffer == NULL || stack_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LCD task buffers");
        if (task_buffer) free(task_buffer);
        if (stack_buffer) free(stack_buffer);
        return ESP_FAIL;
    }

    lcd_task_handle = xTaskCreateStaticPinnedToCore(
        lcd_display_task,
        "lcd_display",
        8192,
        NULL,
        6,  // 优先级 6，高于 LVGL timer (2)
        stack_buffer,
        task_buffer,
        0  // Core 0
    );

    if (lcd_task_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create LCD display task");
        free(task_buffer);
        free(stack_buffer);
        return ESP_FAIL;
    }

    lcd_display_running = true;
    ESP_LOGI(TAG, "LCD display task started");
    return ESP_OK;
}

// 停止 LCD 显示任务
void bsp_camera_lcd_task_stop(void)
{
    if (lcd_task_handle != NULL) {
        vTaskDelete(lcd_task_handle);
        lcd_task_handle = NULL;
        lcd_display_running = false;
        ESP_LOGI(TAG, "LCD display task stopped");
    }
}

// 获取 LCD 显示状态
bool bsp_camera_lcd_display_is_running(void)
{
    return lcd_display_running;
}

void LcdDisplayCameraTaskCreate(void)
{
}
