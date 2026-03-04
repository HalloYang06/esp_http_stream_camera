#ifndef __BSP_LCD_H_
#define __BSP_LCD_H_



#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_spi.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "bsp_pca9557.h"

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

#ifdef __cplusplus
extern "C" {
#endif
esp_err_t bsp_display_brightness_init(void);
esp_err_t bsp_display_brightness_set(int brightness_percent);
esp_err_t bsp_display_backlight_off(void);
esp_err_t bsp_display_backlight_on(void);
void lcd_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *bitmap);
void lcd_set_color(int color);
void lcd_fill_screen(int color);
void lcd_draw_picture(int x,int y,int x_end,int y_end,const unsigned char *pic);
esp_err_t lcd_init();
void lcd_cs(uint8_t level);
void bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock(void);

typedef struct {
    uint32_t max_transfer_sz;
} bsp_display_config_t;

esp_err_t bsp_display_new(const bsp_display_config_t *config,
                          esp_lcd_panel_handle_t *ret_panel,
                          esp_lcd_panel_io_handle_t *ret_io);

#ifdef __cplusplus
}
#endif

#endif