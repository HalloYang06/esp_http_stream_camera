#ifndef _ESP_CAM_H_
#define _ESP_CAM_H_
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h" 
#include "esp_lcd_io_spi.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
/****************   IIC***********************************/
#define I2C_SCL      (GPIO_NUM_2)
#define I2C_SDA      (GPIO_NUM_1)
#define I2C_FREQ_HZ  (100000)
#define I2C_PORT     (I2C_NUM_0)
esp_err_t bsp_iic_init(void);
/***********************************************************/
/***************    LCD显示屏 ↓   *************************/
#define BSP_LCD_PIXEL_CLOCK_HZ     (80 * 1000 * 1000)
#define BSP_LCD_SPI_NUM            (SPI3_HOST)
#define LCD_CMD_BITS               (8)
#define LCD_PARAM_BITS             (8)
#define BSP_LCD_BITS_PER_PIXEL     (16)
#define LCD_LEDC_CH          LEDC_CHANNEL_0

#define BSP_LCD_H_RES              (320)
#define BSP_LCD_V_RES              (240)

#define BSP_LCD_SPI_MOSI      (GPIO_NUM_40)
#define BSP_LCD_SPI_CLK       (GPIO_NUM_41)
#define BSP_LCD_SPI_CS        (GPIO_NUM_NC)
#define BSP_LCD_DC            (GPIO_NUM_39)
#define BSP_LCD_RST           (GPIO_NUM_NC)
#define BSP_LCD_BACKLIGHT     (GPIO_NUM_42)  
esp_err_t bsp_display_brightness_init(void);
esp_err_t bsp_display_brightness_set(int brightness_percent);
esp_err_t bsp_display_backlight_off(void);
esp_err_t bsp_display_backlight_on(void);
esp_err_t display_new();
void lcd_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *bitmap);
void lcd_set_color(int color);
void lcd_fill_screen(int color);
void lcd_draw_picture(int x,int y,int x_end,int y_end,const unsigned char *pic);
esp_err_t lcd_init();
esp_err_t display_new();

/****************    摄像头 ↓   ****************************/
#define CAMERA_PIN_PWDN -1
#define CAMERA_PIN_RESET -1
#define CAMERA_PIN_XCLK 5
#define CAMERA_PIN_SIOD 1
#define CAMERA_PIN_SIOC 2

#define CAMERA_PIN_D7 9
#define CAMERA_PIN_D6 4
#define CAMERA_PIN_D5 6
#define CAMERA_PIN_D4 15
#define CAMERA_PIN_D3 17
#define CAMERA_PIN_D2 8
#define CAMERA_PIN_D1 18
#define CAMERA_PIN_D0 16
#define CAMERA_PIN_VSYNC 3
#define CAMERA_PIN_HREF 46
#define CAMERA_PIN_PCLK 7


#define XCLK_FREQ_HZ 24000000
esp_err_t camera_init();
esp_err_t camera_capture();
void lcd_display_cam();
/****************    摄像头 ↑   ****************************/
/***************    IO扩展芯片 ↓   *************************/
#define PCA9557_INPUT_PORT              0x00
#define PCA9557_OUTPUT_PORT             0x01
#define PCA9557_POLARITY_INVERSION_PORT 0x02
#define PCA9557_CONFIGURATION_PORT      0x03

#define LCD_CS_GPIO                 BIT(0)    // PCA9557_GPIO_NUM_1
#define PA_EN_GPIO                  BIT(1)    // PCA9557_GPIO_NUM_2
#define DVP_PWDN_GPIO               BIT(2)    // PCA9557_GPIO_NUM_3

#define PCA9557_SENSOR_ADDR             0x19        /*!< Slave address of the MPU9250 sensor */

#define SET_BITS(_m, _s, _v)  ((_v) ? (_m)|((_s)) : (_m)&~((_s)))

esp_err_t pca9557_dev_init(void);
void lcd_cs(uint8_t level);
void pa_en(uint8_t level);
void dvp_pwdn(uint8_t level);
void pca9557_init(void);
void pca9557_set_pin(uint8_t pin, uint8_t level);
esp_err_t pca9557_write_register( uint8_t reg_addr, uint8_t data);
/***************    IO扩展芯片 ↑   *************************/

/*************** 摄像头任务管理 ↓ *************************/
// 初始化摄像头采集系统（创建共享缓冲区和采集任务）
esp_err_t camera_tasks_init(void);

// 获取最新帧的副本（用于HTTP和LCD）
// 返回：成功返回帧指针，失败返回NULL
// 注意：调用者负责使用完后调用 camera_frame_release() 释放
camera_fb_t* camera_get_latest_frame(TickType_t timeout_ms);

// 释放帧缓冲区
void camera_frame_release(camera_fb_t *fb);

// 旧的LCD任务创建函数（保持兼容性，内部会调用camera_tasks_init）
void LcdDisplayCameraTaskCreate();
/*************** 摄像头任务管理 ↑ *************************/
/***********************************************************/
#endif