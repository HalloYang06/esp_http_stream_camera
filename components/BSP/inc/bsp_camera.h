#ifndef _BSP_CAMERA_H_
#define _BSP_CAMERA_H_

#include "esp_err.h"
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/****************   Camera Pin Configuration   ********************************/
// DVP Interface Pins
#define BSP_CAMERA_PIN_PWDN         -1
#define BSP_CAMERA_PIN_RESET        -1
#define BSP_CAMERA_PIN_XCLK         (GPIO_NUM_5)
#define BSP_CAMERA_PIN_SIOD         (GPIO_NUM_1)
#define BSP_CAMERA_PIN_SIOC         (GPIO_NUM_2)

#define BSP_CAMERA_PIN_D7           (GPIO_NUM_9)
#define BSP_CAMERA_PIN_D6           (GPIO_NUM_4)
#define BSP_CAMERA_PIN_D5           (GPIO_NUM_6)
#define BSP_CAMERA_PIN_D4           (GPIO_NUM_15)
#define BSP_CAMERA_PIN_D3           (GPIO_NUM_17)
#define BSP_CAMERA_PIN_D2           (GPIO_NUM_8)
#define BSP_CAMERA_PIN_D1           (GPIO_NUM_18)
#define BSP_CAMERA_PIN_D0           (GPIO_NUM_16)
#define BSP_CAMERA_PIN_VSYNC        (GPIO_NUM_3)
#define BSP_CAMERA_PIN_HREF         (GPIO_NUM_46)
#define BSP_CAMERA_PIN_PCLK         (GPIO_NUM_7)

#define BSP_CAMERA_XCLK_FREQ_HZ     (24000000)

/****************   Power Control Configuration   ****************************/
/**
 * @brief Enable this macro to use PCA9557 IO expander for camera power control
 * Comment out this line if using direct GPIO control
 */
#define BSP_CAMERA_USE_PCA9557_PWDN

#ifdef BSP_CAMERA_USE_PCA9557_PWDN
    // Camera power is controlled by PCA9557
    #define BSP_CAMERA_POWER_ON()       bsp_pca9557_dvp_pwdn(0)
    #define BSP_CAMERA_POWER_OFF()      bsp_pca9557_dvp_pwdn(1)
#else
    // Camera power is controlled by direct GPIO
    // Define your GPIO pin here
    #define BSP_CAMERA_PWDN_GPIO        (GPIO_NUM_XX)  // Replace XX with actual GPIO
    #define BSP_CAMERA_POWER_ON()       gpio_set_level(BSP_CAMERA_PWDN_GPIO, 0)
    #define BSP_CAMERA_POWER_OFF()      gpio_set_level(BSP_CAMERA_PWDN_GPIO, 1)
#endif

/****************   Camera Configuration   ***********************************/
// Default camera configuration for GC0308 sensor
#define BSP_CAMERA_PIXEL_FORMAT     PIXFORMAT_RGB565
#define BSP_CAMERA_FRAME_SIZE       FRAMESIZE_QVGA    // 320x240
#define BSP_CAMERA_JPEG_QUALITY     12
#define BSP_CAMERA_FB_COUNT         6
#define BSP_CAMERA_FB_LOCATION      CAMERA_FB_IN_PSRAM
#define BSP_CAMERA_GRAB_MODE        CAMERA_GRAB_WHEN_EMPTY

/**
 * @brief Initialize camera hardware
 * @note Dependencies:
 *       - Must call bsp_i2c_bus_init() first (camera SCCB interface uses I2C)
 *       - If using PCA9557, must call bsp_pca9557_device_init() and bsp_pca9557_init() first
 * @note This function will power on the camera and initialize the camera driver
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_STATE if I2C bus not initialized
 */
esp_err_t bsp_camera_init(void);

/**
 * @brief Capture a single frame (blocking)
 * @note For advanced usage, consider using bsp_camera_tasks_init() instead
 * @return ESP_OK on success
 */
esp_err_t bsp_camera_capture(void);

/****************   Camera Task System   *************************************/
/**
 * @brief Initialize camera capture task system
 * @note Creates a background task that continuously captures frames
 *       Use bsp_camera_get_frame() to retrieve the latest frame
 * @return ESP_OK on success
 */
esp_err_t bsp_camera_tasks_init(void);

/**
 * @brief Get the latest captured frame
 * @param timeout_ms Maximum time to wait for a new frame (milliseconds)
 * @return Pointer to frame copy (must be freed with bsp_camera_frame_free())
 *         Returns NULL on timeout or error
 */
camera_fb_t* bsp_camera_get_frame(TickType_t timeout_ms);

/**
 * @brief Free a frame obtained from bsp_camera_get_frame()
 * @param fb Frame buffer to free
 */
void bsp_camera_frame_free(camera_fb_t *fb);

/**
 * @brief Legacy function name for compatibility
 */
void LcdDisplayCameraTaskCreate(void);

#ifdef __cplusplus
}
#endif

#endif // _BSP_CAMERA_H_
