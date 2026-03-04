#include "bsp_lcd.h"
#include "string.h"
/***************************LCD CODE START*******************************/
void lcd_cs(uint8_t level){
    pca9557_set_pin(LCD_CS_GPIO, level); //PCA9557_GPIO_NUM_1
}
// 背光PWM初始化
static const char *TAG = "BSP_LCD";
esp_err_t bsp_display_brightness_init(void)
{
    // Setup LEDC peripheral for PWM backlight control
    const ledc_channel_config_t LCD_backlight_channel = {
        .gpio_num = BSP_LCD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LCD_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = 0,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = true
    };
    const ledc_timer_config_t LCD_backlight_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = 0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };

    ESP_ERROR_CHECK(ledc_timer_config(&LCD_backlight_timer));
    ESP_ERROR_CHECK(ledc_channel_config(&LCD_backlight_channel));

    return ESP_OK;
}

// 背光亮度设置
esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    if (brightness_percent > 100) {
        brightness_percent = 100;
    } else if (brightness_percent < 0) {
        brightness_percent = 0;
    }

    ESP_LOGI(TAG, "Setting LCD backlight: %d%%", brightness_percent);
    // LEDC resolution set to 10bits, thus: 100% = 1023
    uint32_t duty_cycle = (1023 * brightness_percent) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH, duty_cycle));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH));

    return ESP_OK;
}

// 关闭背光
esp_err_t bsp_display_backlight_off(void)
{
    return bsp_display_brightness_set(0);
}

// 打开背光 最亮
esp_err_t bsp_display_backlight_on(void)
{   
    
    return bsp_display_brightness_set(50);
}

// 定义液晶屏句柄（全局变量，供外部使用）
esp_lcd_panel_handle_t panel_handle = NULL;
esp_lcd_panel_io_handle_t io_handle = NULL;

// DMA缓冲区用于分块传输（每次传输50行）
#define LCD_DMA_CHUNK_LINES 50
static uint8_t *lcd_dma_buffer = NULL;
esp_err_t display_new(){
    ESP_LOGI("LCD", "display_new函数开始执行");
    //initialize the LCD
    esp_err_t ret = ESP_OK;
    //初始化背光
    ret=bsp_display_brightness_init();
    if(ret!=ESP_OK){
        ESP_LOGI("LCD", "Brightness init failed");
    }
    //初始化SPI总线
    ESP_LOGI("LCD", "Initializing SPI bus for LCD");
    const spi_bus_config_t bus_config = {
        .miso_io_num = -1,
        .mosi_io_num = BSP_LCD_SPI_MOSI,
        .sclk_io_num = BSP_LCD_SPI_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BSP_LCD_H_RES * LCD_DMA_CHUNK_LINES * sizeof(uint16_t),
    };
    ret=spi_bus_initialize(BSP_LCD_SPI_NUM, &bus_config, SPI_DMA_CH_AUTO);
    if(ret!=ESP_OK){
        ESP_LOGI("LCD", "SPI bus init failed");
        goto err;
    }
    //初始化LCD面板IO
    ESP_LOGI("LCD", "Initializing LCD panel IO");
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = BSP_LCD_DC,
        .cs_gpio_num = BSP_LCD_SPI_CS,
        .pclk_hz=BSP_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits=8,
        .lcd_param_bits=8,
        .spi_mode=2,
        .trans_queue_depth=10,
        .flags = {
            .dc_low_on_data = 0,
            .octal_mode = 0,
            .sio_mode = 0,
            .lsb_first = 0,
            .cs_high_active = 0,
        }
    };
    ret=esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM, &io_config, &io_handle);
    if(ret!=ESP_OK){
        ESP_LOGI("LCD", "LCD IO init failed");
        goto err;
    }
    //初始化LCD面板驱动
    ESP_LOGI("LCD", "Initializing LCD panel driver");
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_RST,
        .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
    };
    ret=esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);
    if(ret!=ESP_OK){
        ESP_LOGI("LCD", "LCD panel init failed");
        goto err;
    }
    esp_lcd_panel_reset(panel_handle);
    lcd_cs(0); // 使能LCD片选
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_invert_color(panel_handle, true);
    // 恢复原始配置
    esp_lcd_panel_swap_xy(panel_handle, true);
    esp_lcd_panel_mirror(panel_handle, false, false);

    // 分配DMA缓冲区（必须在内部RAM中用于SPI DMA）
    size_t dma_buffer_size = BSP_LCD_H_RES * LCD_DMA_CHUNK_LINES * sizeof(uint16_t);
    lcd_dma_buffer = (uint8_t *)heap_caps_malloc(dma_buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!lcd_dma_buffer) {
        ESP_LOGE("LCD", "Failed to allocate DMA buffer (%zu bytes)", dma_buffer_size);
        ret = ESP_ERR_NO_MEM;
        goto err;
    }
    ESP_LOGI("LCD", "DMA buffer allocated: %zu bytes", dma_buffer_size);

    return ret;
    err:
    if (lcd_dma_buffer) {
        heap_caps_free(lcd_dma_buffer);
        lcd_dma_buffer = NULL;
    }
    if (panel_handle) {
        esp_lcd_panel_del(panel_handle);
    }
    if (io_handle) {
        esp_lcd_panel_io_del(io_handle);
    }
    spi_bus_free(BSP_LCD_SPI_NUM);
    return ret;

}
void lcd_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *bitmap){
    // ESP32-S3的SPI DMA无法直接访问PSRAM，需要分块传输
    if (!lcd_dma_buffer) {
        ESP_LOGE(TAG, "DMA buffer not allocated");
        return;
    }

    int width = x_end - x_start;
    int height = y_end - y_start;
    const uint8_t *src = (const uint8_t *)bitmap;
    size_t bytes_per_line = width * sizeof(uint16_t);

    // 分块传输，每次最多LCD_DMA_CHUNK_LINES行
    for (int y = 0; y < height; y += LCD_DMA_CHUNK_LINES) {
        int chunk_lines = (y + LCD_DMA_CHUNK_LINES <= height) ? LCD_DMA_CHUNK_LINES : (height - y);
        size_t chunk_size = bytes_per_line * chunk_lines;

        // 从PSRAM复制到内部DMA缓冲区
        // 数据已经在WhoFetchNode中转换为大端，这里直接复制
        memcpy(lcd_dma_buffer, src + y * bytes_per_line, chunk_size);

        // 传输这个块
        esp_err_t ret = esp_lcd_panel_draw_bitmap(panel_handle,
                                                   x_start,
                                                   y_start + y,
                                                   x_end,
                                                   y_start + y + chunk_lines,
                                                   lcd_dma_buffer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to draw chunk at y=%d: %s", y, esp_err_to_name(ret));
            return;
        }
    }
}
void lcd_set_color(int color){
    uint16_t *buffer = (uint16_t *)heap_caps_malloc(BSP_LCD_H_RES * sizeof(uint16_t), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for color buffer");
        return;
    }
    else {
        ESP_LOGI(TAG, "Memory for color buffer allocated");
        for (int i=0;i<BSP_LCD_H_RES;i++){
            buffer[i]=color;
        }
        for (int y=0;y<BSP_LCD_V_RES;y++){
            esp_lcd_panel_draw_bitmap(panel_handle, 0, y, BSP_LCD_H_RES, y+1, buffer);
        }
        free(buffer);
    }
    // Fill the buffer with the specified color
}
void lcd_fill_screen(int color){
    lcd_set_color(color);
}
void lcd_draw_picture(int x,int y,int x_end,int y_end,const unsigned char *pic){
    size_t pixel_size =(x_end - x) * (y_end - y)*2;
    uint16_t *buffer = (uint16_t *)heap_caps_malloc(pixel_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for picture buffer");
        return;
    }
    memcpy(buffer, pic, pixel_size);
    esp_lcd_panel_draw_bitmap(panel_handle, x, y, x_end, y_end,(uint16_t *)buffer);
    free(buffer);       
}
esp_err_t lcd_init(){
    esp_err_t ret = ESP_OK;
    ret=display_new();
   
    // Black
    ret=esp_lcd_panel_disp_on_off(panel_handle,true);
    if(ret!=ESP_OK){
        ESP_LOGI("LCD", "打开失败");
        return ret;
    }else{
        ESP_LOGI("LCD", "打开成功");
    }
    lcd_set_color(0xF800); 
    bsp_display_backlight_on();
    return ret;
}
void lcd_display_cam(){
    //get a frame
    camera_fb_t * fb = esp_camera_fb_get();

    //display the image on LCD
    //replace this with your own function
    //display_image(fb->width, fb->height, fb->format, fb->buf, fb->len);

    //return the frame buffer back to be reused
    esp_camera_fb_return(fb);
}
/***************************LCD CODE END**********************************/

/****************   LVGL Thread Safety (ESP-WHO Compatibility)   *************/
static SemaphoreHandle_t lvgl_mutex = NULL;

// Initialize LVGL mutex (lazy initialization)
static void lvgl_mutex_init(void)
{
    if (lvgl_mutex == NULL) {
        lvgl_mutex = xSemaphoreCreateRecursiveMutex();
        assert(lvgl_mutex != NULL);
        ESP_LOGI(TAG, "LVGL mutex initialized");
    }
}

void bsp_display_lock(uint32_t timeout_ms){
    lvgl_mutex_init();
    const TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    xSemaphoreTakeRecursive(lvgl_mutex, timeout_ticks);
}

void bsp_display_unlock(void){
    lvgl_mutex_init();
    xSemaphoreGiveRecursive(lvgl_mutex);
}

esp_err_t bsp_display_new(const bsp_display_config_t *config,
                          esp_lcd_panel_handle_t *ret_panel,
                          esp_lcd_panel_io_handle_t *ret_io)
{
    ESP_LOGI(TAG, "BSP display initializing (ESP-WHO compatibility)");

    esp_err_t ret = display_new();

    if (ret == ESP_OK) {
        if (ret_panel != NULL) {
            *ret_panel = panel_handle;
        }
        if (ret_io != NULL) {
            *ret_io = io_handle;
        }
        ESP_LOGI(TAG, "BSP display initialized successfully");
    } else {
        ESP_LOGE(TAG, "BSP display initialization failed");
    }

    return ret;
}