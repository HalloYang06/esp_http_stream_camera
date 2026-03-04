#include "bsp_lvgl.h"
#include "bsp_lcd.h"
#include "bsp_touch.h"
#include "bsp_camera.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"

static const char *TAG = "bsp_lvgl";
static lv_display_t *g_display = NULL;
static lv_indev_t *g_touch_indev = NULL;
static esp_timer_handle_t lvgl_tick_timer = NULL;
static TaskHandle_t lvgl_timer_task_handle = NULL;
static bool lvgl_timer_paused = false;

// 摄像头显示相关
static lv_obj_t *camera_img = NULL;
static lv_image_dsc_t camera_img_dsc;
static uint8_t *display_frame_buf = NULL;  // 独立显示缓冲区（PSRAM）

// 硬件定时器回调（替代 tick 任务）
static void lvgl_tick_timer_cb(void *arg)
{
    lv_tick_inc(5);
}

// LVGL Timer 任务
static void lvgl_timer_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL Timer Task Started on Core %d", xPortGetCoreID());

    while (1) {
        // 检查是否暂停
        if (!lvgl_timer_paused) {
            bsp_display_lock(0);
            uint32_t delay = lv_timer_handler();
            bsp_display_unlock();

            // 限制最小延迟为20ms，减少 CPU 占用
            if (delay == 0 || delay > 100) {
            delay = 20;
        }
        vTaskDelay(pdMS_TO_TICKS(delay));
        taskYIELD();  // 主动让出 CPU
        }
    }
}
// Flush 回调函数
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    static uint32_t flush_count = 0;

    bsp_display_lock(0);

    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2 + 1;
    int y2 = area->y2 + 1;

    
    lcd_draw_bitmap(x1, y1, x2, y2, px_map);

    bsp_display_unlock();

    lv_display_flush_ready(disp);

    flush_count++;
    if (flush_count % 100 == 0) {
        ESP_LOGI(TAG, "Flush callback called %lu times", flush_count);
    }
}

// 初始化 LVGL
esp_err_t bsp_lvgl_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL");

    // 1. 初始化 LVGL 核心
    lv_init();
    ESP_LOGI(TAG, "LVGL core initialized");

    // 2. 创建显示驱动
    g_display = lv_display_create(320, 240);
    if (g_display == NULL) {
        ESP_LOGE(TAG, "Failed to create display");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Display created: 320x240");

    // 3. 分配绘制缓冲区（内部 RAM，用于 DMA）
    // 大小：320 * 10 * 2 = 6.4KB（每个缓冲区）
    size_t buf_size = 320 * 10 * sizeof(uint16_t);
    void *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    if (buf1 == NULL || buf2 == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffers");
        if (buf1) heap_caps_free(buf1);
        if (buf2) heap_caps_free(buf2);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "LVGL buffers allocated: %zu bytes each (buf1=%p, buf2=%p)", buf_size, buf1, buf2);

    // 4. 设置缓冲区（部分渲染模式，双缓冲）
    lv_display_set_buffers(g_display, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    ESP_LOGI(TAG, "Display buffers configured (PARTIAL mode)");

    // 5. 设置 flush 回调
    lv_display_set_flush_cb(g_display, lvgl_flush_cb);
    ESP_LOGI(TAG, "Flush callback registered");

    // 6. 设置颜色格式
    lv_display_set_color_format(g_display, LV_COLOR_FORMAT_RGB565);
    ESP_LOGI(TAG, "Color format set to RGB565");



    // 8. 设置默认屏幕背景为透明，让直接绘制的摄像头画面显示
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);  // 透明背景
    ESP_LOGI(TAG, "Screen background set to transparent");

    ESP_LOGI(TAG, "LVGL initialized successfully");
    return ESP_OK;
}

// 启动 LVGL 任务
esp_err_t bsp_lvgl_start_tasks(void)
{
    ESP_LOGI(TAG, "Starting LVGL tasks");

    // 使用硬件定时器替代 tick 任务（避免看门狗问题）
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &lvgl_tick_timer_cb,
        .name = "lvgl_tick",
        .skip_unhandled_events = true,
    };

    esp_err_t ret = esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LVGL tick timer");
        return ESP_FAIL;
    }

    // 启动定时器，每5ms触发一次
    ret = esp_timer_start_periodic(lvgl_tick_timer, 5000);  // 5000 微秒 = 5ms
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start LVGL tick timer");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LVGL tick timer started (5ms period)");

    // 创建 Timer 任务（使用 PSRAM 栈）
    ESP_LOGI(TAG, "Creating LVGL timer task with PSRAM stack...");

    StaticTask_t *task_buffer = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
    StackType_t *stack_buffer = (StackType_t *)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);

    if (task_buffer == NULL || stack_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LVGL timer task buffers");
        if (task_buffer) free(task_buffer);
        if (stack_buffer) free(stack_buffer);
        return ESP_FAIL;
    }

    lvgl_timer_task_handle = xTaskCreateStaticPinnedToCore(
        lvgl_timer_task,
        "lvgl_timer",
        8192,
        NULL,
        2,
        stack_buffer,
        task_buffer,
        0  // Core 0
    );

    if (lvgl_timer_task_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL timer task");
        free(task_buffer);
        free(stack_buffer);
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

// 触摸输入读取回调
static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t x, y;
    bool pressed;

    esp_err_t ret = bsp_touch_read(&x, &y, &pressed);

    if (ret == ESP_OK) {
        if (pressed) {
            data->point.x = x;
            data->point.y = y;
            data->state = LV_INDEV_STATE_PRESSED;

            // 输出触摸坐标到日志
            ESP_LOGI(TAG, "Touch: X=%d, Y=%d", x, y);
        } else {
            data->state = LV_INDEV_STATE_RELEASED;
        }
    } else {
        // 读取失败，返回释放状态
        data->state = LV_INDEV_STATE_RELEASED;
    }

    // 让出 CPU，避免阻塞其他任务
    taskYIELD();
}

// 初始化触摸输入设备
esp_err_t bsp_lvgl_touch_init(void)
{
    ESP_LOGI(TAG, "Initializing touch input device");

    // 初始化触摸硬件
    esp_lcd_touch_handle_t touch_handle = bsp_touch_init();
    if (touch_handle == NULL) {
        ESP_LOGE(TAG, "Failed to initialize touch hardware");
        return ESP_FAIL;
    }

    // 创建 LVGL 输入设备
    g_touch_indev = lv_indev_create();
    if (g_touch_indev == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL input device");
        return ESP_FAIL;
    }

    lv_indev_set_type(g_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(g_touch_indev, lvgl_touch_read_cb);

    ESP_LOGI(TAG, "Touch input device initialized successfully");
    return ESP_OK;
}

// 摄像头显示任务
static void camera_display_task(void *arg)
{
    ESP_LOGI(TAG, "Camera Display Task Started on Core %d", xPortGetCoreID());

    // 分配独立的显示缓冲区（PSRAM）
    size_t frame_size = 320 * 240 * sizeof(uint16_t);  // 153.6KB
    display_frame_buf = (uint8_t *)heap_caps_malloc(frame_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);

    if (display_frame_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate display frame buffer");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Display frame buffer allocated: %zu bytes", frame_size);

    // 创建图像描述符
    camera_img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    camera_img_dsc.header.w = 320;
    camera_img_dsc.header.h = 240;
    camera_img_dsc.data_size = frame_size;
    camera_img_dsc.data = display_frame_buf;

    // 初始化缓冲区（黑色）
    memset(display_frame_buf, 0, frame_size);

    // 创建图像对象显示摄像头
    bsp_display_lock(0);
    camera_img = lv_image_create(lv_screen_active());
    lv_image_set_src(camera_img, &camera_img_dsc);
    lv_obj_align(camera_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_background(camera_img);
    bsp_display_unlock();

    ESP_LOGI(TAG, "Camera image object created");

    uint32_t frame_count = 0;
    TickType_t last_update = xTaskGetTickCount();
    bool first_frame = true;

    while (1) {
        // 获取最新帧（等待最多100ms）
        camera_fb_t *fb = bsp_camera_get_frame(100);

        if (fb != NULL && fb->len == frame_size) {
            // 第一帧时输出调试信息
            if (first_frame) {
                ESP_LOGI(TAG, "First frame received: width=%d, height=%d, format=%d, len=%zu",
                         fb->width, fb->height, fb->format, fb->len);
                // 输出前16个字节的数据
                ESP_LOGI(TAG, "First 16 bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                         fb->buf[0], fb->buf[1], fb->buf[2], fb->buf[3],
                         fb->buf[4], fb->buf[5], fb->buf[6], fb->buf[7],
                         fb->buf[8], fb->buf[9], fb->buf[10], fb->buf[11],
                         fb->buf[12], fb->buf[13], fb->buf[14], fb->buf[15]);
                first_frame = false;
            }

            // 限制刷新率为约15fps（每66ms更新一次）
            TickType_t now = xTaskGetTickCount();
            if ((now - last_update) >= pdMS_TO_TICKS(66)) {
                // 直接复制帧数据到显示缓冲区
                bsp_display_lock(0);
                memcpy(display_frame_buf, fb->buf, frame_size);

                // 触发图像对象重绘
                lv_obj_invalidate(camera_img);
                bsp_display_unlock();

                // 释放帧
                bsp_camera_frame_free(fb);

                last_update = now;
                frame_count++;
                if (frame_count % 50 == 0) {
                    ESP_LOGI(TAG, "Displayed %lu frames", frame_count);
                }
            } else {
                // 释放帧但不更新显示
                bsp_camera_frame_free(fb);
            }
        } else {
            if (fb != NULL) {
                ESP_LOGW(TAG, "Frame size mismatch: expected %zu, got %zu", frame_size, fb->len);
                bsp_camera_frame_free(fb);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    vTaskDelete(NULL);
}

// 创建摄像头显示任务
esp_err_t bsp_lvgl_camera_task_create(void)
{
    ESP_LOGI(TAG, "Creating camera display task");

    BaseType_t ret = xTaskCreatePinnedToCore(
        camera_display_task,
        "cam_display",
        6144,
        NULL,
        6,
        NULL,
        0  // Core 0
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create camera display task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Camera display task created successfully");
    return ESP_OK;
}

// 暂停 LVGL Timer 任务
void bsp_lvgl_timer_pause(void)
{
    lvgl_timer_paused = true;
    ESP_LOGI(TAG, "LVGL Timer paused");
}

// 恢复 LVGL Timer 任务
void bsp_lvgl_timer_resume(void)
{
    lvgl_timer_paused = false;
    ESP_LOGI(TAG, "LVGL Timer resumed");
}
