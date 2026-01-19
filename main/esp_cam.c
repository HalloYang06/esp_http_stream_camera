#include <stdio.h>
#include "esp_cam.h"
static const char *TAG = "esp_cam";
i2c_master_bus_handle_t i2c_bus=NULL;//i2c总线句柄
/****************IIC ***************************************/
i2c_master_dev_handle_t PCA9557_dev_handle=NULL;//PCA9557 i2c设备句柄
esp_err_t bsp_iic_init(void){
    i2c_master_bus_config_t bus_config = {
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7, 
        .flags = {
            .enable_internal_pullup = 1,
            .allow_pd = 0,
        },
    };
    esp_err_t ret = i2c_new_master_bus(&bus_config, &i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C总线初始化失败，错误码: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "检查引脚是否被占用、配置是否正确（SDA=%d, SCL=%d）", I2C_SDA, I2C_SCL);
    } else {
        ESP_LOGI(TAG, "I2C总线初始化成功，句柄: %p，端口: I2C_NUM_%d", i2c_bus, I2C_PORT);
    }
    return ret;
}
/***********************************************************/
/***************    IO扩展芯片 ↓   *************************/
//初始化iic设备
esp_err_t pca9557_dev_init(void){
    if(i2c_bus==NULL){
        ESP_LOGE("PCA9557", "I2C Bus 未初始化，无法添加设备");
        return ESP_ERR_INVALID_STATE;
    }
    
    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_7,
        .device_address = PCA9557_SENSOR_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &device_config, &PCA9557_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PCA9557 设备添加失败，错误码: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "检查设备地址是否正确（地址=0x%02X）", PCA9557_SENSOR_ADDR);
    } else {
        ESP_LOGI(TAG, "PCA9557 设备添加成功，句柄: %p", PCA9557_dev_handle);
    }
    return ret;
}

//读取PCA9557寄存器值
esp_err_t pca9557_read_register( uint8_t reg_addr, uint8_t *data, size_t len)
{
    if(i2c_bus==NULL||len==0||data==NULL){
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(PCA9557_dev_handle,&reg_addr,1,data,len,pdMS_TO_TICKS(1000));
}

//写PCA9557寄存器值
esp_err_t pca9557_write_register( uint8_t reg_addr, uint8_t data)
{
    if(i2c_bus==NULL){  // 移除data==NULL检查，data是uint8_t不是指针
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t write_buf[2] = {reg_addr, data};
    esp_err_t ret = i2c_master_transmit(PCA9557_dev_handle, write_buf, sizeof(write_buf), pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PCA9557 write reg 0x%02X = 0x%02X failed: %s", reg_addr, data, esp_err_to_name(ret));
    }
    return ret;
}
//初始化PCA9557
void pca9557_init(void){
    //初始化输出寄存器
    pca9557_write_register(PCA9557_OUTPUT_PORT, 0x05); //全部输出低电平
    //配置端口方向寄存器，0为输出，1为输入
    pca9557_write_register(PCA9557_CONFIGURATION_PORT, 0x00); //全部配置为输出
    
}
//设置芯片某个引脚的电平
void pca9557_set_pin(uint8_t pin, uint8_t level){
    uint8_t output_state;
    pca9557_read_register(PCA9557_OUTPUT_PORT, &output_state, 1);
    ESP_LOGI(TAG, "PCA9557 before: 0x%02X, setting pin 0x%02X to %d", output_state, pin, level);
    output_state = SET_BITS(output_state, pin, level); // pin已经是BIT(x)形式，不需要再移位
    pca9557_write_register(PCA9557_OUTPUT_PORT, output_state);
    ESP_LOGI(TAG, "PCA9557 after: 0x%02X", output_state);
}
void lcd_cs(uint8_t level){
    pca9557_set_pin(LCD_CS_GPIO, level); //PCA9557_GPIO_NUM_1
}
void pa_en(uint8_t level){
    pca9557_set_pin(PA_EN_GPIO, level); //PCA9557_GPIO_NUM_2
}
void dvp_pwdn(uint8_t level){
    pca9557_set_pin(DVP_PWDN_GPIO, level); //PCA9557_GPIO_NUM_3
}
/***************    IO扩展芯片 ↑   *************************/
/***********************************************************/

/*************************CAMERA CODE START*****************
***********************************************************/
static camera_config_t camera_config = {
    .pin_pwdn  = CAMERA_PIN_PWDN,
    .pin_reset = CAMERA_PIN_RESET,
    .pin_xclk = CAMERA_PIN_XCLK,
    .pin_sccb_sda = -1,
    .pin_sccb_scl = -1,

    .pin_d7 = CAMERA_PIN_D7,
    .pin_d6 = CAMERA_PIN_D6,
    .pin_d5 = CAMERA_PIN_D5,
    .pin_d4 = CAMERA_PIN_D4,
    .pin_d3 = CAMERA_PIN_D3,
    .pin_d2 = CAMERA_PIN_D2,
    .pin_d1 = CAMERA_PIN_D1,
    .pin_d0 = CAMERA_PIN_D0,
    .pin_vsync = CAMERA_PIN_VSYNC,
    .pin_href = CAMERA_PIN_HREF,
    .pin_pclk = CAMERA_PIN_PCLK,

    .xclk_freq_hz = 24000000,
    .ledc_timer = LEDC_TIMER_1,     // 改为TIMER_1，避免与LCD背光的TIMER_0冲突
    .ledc_channel = LEDC_CHANNEL_1, // 改为CHANNEL_1

    .pixel_format = PIXFORMAT_RGB565,  // GC0308只支持RGB565，不支持JPEG
    .frame_size = FRAMESIZE_QVGA,      // QVGA (320x240)

    .jpeg_quality = 12, // JPEG编码质量（用于RGB转JPEG）
    .fb_count = 2,      // 使用双缓冲
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    .fb_location=CAMERA_FB_IN_PSRAM,


    .sccb_i2c_port = I2C_PORT,
};

esp_err_t camera_init(){
    //power up the camera if PWDN pin is defined
    if(CAMERA_PIN_PWDN != -1){
        //pinMode(CAMERA_PIN_PWDN, OUTPUT);
        //digitalWrite(CAMERA_PIN_PWDN, LOW);
        gpio_set_direction(CAMERA_PIN_PWDN, GPIO_MODE_OUTPUT);
        gpio_set_level(CAMERA_PIN_PWDN, 0);
    }

    //initialize the camera
    ESP_LOGI(TAG, "Opening camera power via PCA9557");
    dvp_pwdn(0); // 打开摄像头电源（低电平有效）
    vTaskDelay(pdMS_TO_TICKS(100)); // 等待摄像头上电稳定，至少需要50ms

    ESP_LOGI(TAG, "Initializing camera");
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Camera Init OK");
    return ESP_OK;
}

esp_err_t camera_capture(){
    //acquire a frame
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera Capture Failed");
        return ESP_FAIL;
    }
    //replace this with your own function
   // process_image(fb->width, fb->height, fb->format, fb->buf, fb->len);
  
    //return the frame buffer back to the driver for reuse
    esp_camera_fb_return(fb);
    return ESP_OK;
}
/***************************************************************************
***************************************************************************
* CAMERA CODE END***********************************************************
***************************************************************************
***************************************************************************/

/***************************LCD CODE START*******************************/
// 背光PWM初始化
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

// 定义液晶屏句柄
static esp_lcd_panel_handle_t panel_handle = NULL;
esp_lcd_panel_io_handle_t io_handle = NULL;
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
        .max_transfer_sz = BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(uint16_t)
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
        .trans_queue_depth=20,
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
    esp_lcd_panel_swap_xy(panel_handle, true);
    esp_lcd_panel_mirror(panel_handle, true, false);

    return ret;
    err:
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
    esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start, x_end, y_end, bitmap);
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
/************************任务处理***************************************/
QueueHandle_t camera_queue = NULL; // 创建摄像头队列

static void camera_task(void *arg) {
    camera_fb_t *fb = NULL;
    ESP_LOGI(TAG, "Camera Task Started");
    const TickType_t frame_delay = pdMS_TO_TICKS(50); // 限制帧率到~20fps，减少撕裂

    while (1) {
        fb = esp_camera_fb_get();
        if (fb == NULL){
            ESP_LOGE(TAG, "Camera Capture Failed");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // 发送帧到队列，如果队列满则跳过该帧
        if (xQueueSend(camera_queue, &fb, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Queue full, dropping frame");
            esp_camera_fb_return(fb); // 队列满时释放帧
        }

        vTaskDelay(frame_delay); // 控制采集帧率
    }
    vTaskDelete(NULL);
}
static void lcd_task(void *arg) {
    camera_fb_t *fb = NULL;
    ESP_LOGI(TAG, "LCD Task Started");
    const uint8_t per_line = 20; // 增加到60行，减少分块次数，提升流畅度
    while (1) {
        if (xQueueReceive(camera_queue, &fb, portMAX_DELAY)) {
            uint16_t *buf = (uint16_t *)fb->buf; // 先转换为uint16_t指针
            for(uint16_t y = 0; y < fb->height; y += per_line){
                uint16_t lines_to_draw = (y + per_line <= fb->height) ? per_line : (fb->height - y);
                // 修正指针偏移计算：RGB565每像素2字节
                esp_lcd_panel_draw_bitmap(panel_handle, 0, y, fb->width, y + lines_to_draw,
                            (uint16_t *)(buf + y * fb->width));
                taskYIELD(); // 使用yield代替delay，减少撕裂
            }
            esp_camera_fb_return(fb);
        }
    }
    vTaskDelete(NULL);
}
void LcdDisplayCameraTaskCreate(){
    ESP_LOGI(TAG, "Creating Camera and LCD Tasks");
    camera_queue = xQueueCreate(2, sizeof(camera_fb_t *)); // 创建一个队列，最多存储2个摄像头帧指针
    if (camera_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create camera queue");
        return;
    }
    // Camera任务：优先级4，在Core 1运行
    BaseType_t xReturned = xTaskCreatePinnedToCore(camera_task, "camera_task", 8192, NULL, 4, NULL, 1);
    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "Failed to create camera task");
        return;
    }
    // LCD任务：优先级6（更高），在Core 0运行，优先完成绘制减少撕裂
    xReturned = xTaskCreatePinnedToCore(lcd_task, "lcd_task", 6144, NULL, 6, NULL, 0);
    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LCD task");
        return;
    }
    ESP_LOGI(TAG, "Camera and LCD Tasks Created Successfully");
}