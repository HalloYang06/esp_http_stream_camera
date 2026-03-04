#include "bsp_lvgl.h"
#include "bsp_lcd.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bsp_lvgl";
static lv_display_t *g_display = NULL;

// LVGL Tick 任务
static void lvgl_tick_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL Tick Task Started on Core %d", xPortGetCoreID());

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5));
        lv_tick_inc(5);
    }
}

// LVGL Timer 任务
static void lvgl_timer_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL Timer Task Started on Core %d", xPortGetCoreID());

    while (1) {
        bsp_display_lock(0);
        uint32_t delay = lv_timer_handler();
        bsp_display_unlock();

        // 限制最小延迟为5ms
        if (delay == 0 || delay > 100) {
            delay = 5;
        }
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
}

// Flush 回调函数
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    bsp_display_lock(0);

    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2 + 1;
    int y2 = area->y2 + 1;

    // 复用 bsp_lcd.c 的分块传输函数
    lcd_draw_bitmap(x1, y1, x2, y2, px_map);

    bsp_display_unlock();

    lv_display_flush_ready(disp);
}

// 初始化 LVGL
esp_err_t bsp_lvgl_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL");

    // 1. 初始化 LVGL 核心
    lv_init();

    // 2. 创建显示驱动
    g_display = lv_display_create(320, 240);
    if (g_display == NULL) {
        ESP_LOGE(TAG, "Failed to create display");
        return ESP_FAIL;
    }

    // 3. 分配绘制缓冲区（内部 RAM，用于 DMA）
    // 大小：320 * 20 * 2 = 12.8KB（每个缓冲区）
    size_t buf_size = 320 * 20 * sizeof(uint16_t);
    void *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    if (buf1 == NULL || buf2 == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffers");
        if (buf1) heap_caps_free(buf1);
        if (buf2) heap_caps_free(buf2);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "LVGL buffers allocated: %zu bytes each", buf_size);

    // 4. 设置缓冲区（部分渲染模式，双缓冲）
    lv_display_set_buffers(g_display, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // 5. 设置 flush 回调
    lv_display_set_flush_cb(g_display, lvgl_flush_cb);

    // 6. 设置颜色格式
    lv_display_set_color_format(g_display, LV_COLOR_FORMAT_RGB565);

    ESP_LOGI(TAG, "LVGL initialized successfully");
    return ESP_OK;
}

// 启动 LVGL 任务
esp_err_t bsp_lvgl_start_tasks(void)
{
    ESP_LOGI(TAG, "Starting LVGL tasks");

    // 创建 Tick 任务（Core 0，优先级3）
    BaseType_t ret = xTaskCreatePinnedToCore(
        lvgl_tick_task,
        "lvgl_tick",
        2048,
        NULL,
        3,
        NULL,
        0  // Core 0
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL tick task");
        return ESP_FAIL;
    }

    // 创建 Timer 任务（Core 0，优先级5）
    ret = xTaskCreatePinnedToCore(
        lvgl_timer_task,
        "lvgl_timer",
        4096,
        NULL,
        5,
        NULL,
        0  // Core 0
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL timer task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LVGL tasks started successfully");
    return ESP_OK;
}

// 获取显示驱动句柄
lv_display_t* bsp_lvgl_get_display(void)
{
    return g_display;
}
