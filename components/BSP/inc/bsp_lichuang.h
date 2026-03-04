#ifndef _BSP_LICHUANG_H_
#define _BSP_LICHUANG_H_

// Include all BSP modules
#include "bsp_i2cbus.h"
#include "bsp_pca9557.h"
#include "bsp_camera.h"
#include "bsp_lcd.h"
#include "bsp_touch.h"

#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/****************   BSP Configuration Macros   *******************************/
/**
 * @brief Camera configuration compatible with ESP-WHO framework
 */
#define BSP_CAMERA_DEFAULT_CONFIG {                       \
    .pin_pwdn  = BSP_CAMERA_PIN_PWDN,                     \
    .pin_reset = BSP_CAMERA_PIN_RESET,                    \
    .pin_xclk = BSP_CAMERA_PIN_XCLK,                      \
    .pin_sccb_sda = -1,                                   \
    .pin_sccb_scl = -1,                                   \
    .pin_d7 = BSP_CAMERA_PIN_D7,                          \
    .pin_d6 = BSP_CAMERA_PIN_D6,                          \
    .pin_d5 = BSP_CAMERA_PIN_D5,                          \
    .pin_d4 = BSP_CAMERA_PIN_D4,                          \
    .pin_d3 = BSP_CAMERA_PIN_D3,                          \
    .pin_d2 = BSP_CAMERA_PIN_D2,                          \
    .pin_d1 = BSP_CAMERA_PIN_D1,                          \
    .pin_d0 = BSP_CAMERA_PIN_D0,                          \
    .pin_vsync = BSP_CAMERA_PIN_VSYNC,                    \
    .pin_href = BSP_CAMERA_PIN_HREF,                      \
    .pin_pclk = BSP_CAMERA_PIN_PCLK,                      \
    .xclk_freq_hz = BSP_CAMERA_XCLK_FREQ_HZ,              \
    .ledc_timer = LEDC_TIMER_1,                           \
    .ledc_channel = LEDC_CHANNEL_1,                       \
    .pixel_format = PIXFORMAT_RGB565,                     \
    .frame_size = FRAMESIZE_QVGA,                         \
    .jpeg_quality = 12,                                   \
    .fb_count = 2,                                        \
    .fb_location = CAMERA_FB_IN_PSRAM,                    \
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,                  \
    .sccb_i2c_port = BSP_I2C_PORT,                        \
}

/**
 * @brief LCD draw buffer configuration (for ESP-WHO framework)
 */
#define BSP_LCD_DRAW_BUFF_SIZE      (BSP_LCD_H_RES * 50)
#define BSP_LCD_DRAW_BUFF_DOUBLE    true

/****************   BSP Initialization   *************************************/
/**
 * @brief Initialize all BSP peripherals
 * @note This function initializes I2C, PCA9557, and powers on camera
 * @return ESP_OK on success
 */
static inline esp_err_t bsp_board_init(void)
{
    esp_err_t ret = ESP_OK;

    // Initialize I2C bus
    ret = bsp_i2c_bus_init();
    if (ret != ESP_OK) {
        return ret;
    }

    // Initialize PCA9557 IO expander
    ret = pca9557_dev_init();
    if (ret != ESP_OK) {
        return ret;
    }

    pca9557_init();


    // Power on camera (DVP_PWDN = 0, active low)
    pca9557_set_pin(DVP_PWDN_GPIO, 0); //PCA9557_GPIO_NUM_2
    vTaskDelay(pdMS_TO_TICKS(10));  // Wait for camera to stabilize

    return ESP_OK;
}

#ifdef __cplusplus
}
#endif

#endif // _BSP_LICHUANG_H_
