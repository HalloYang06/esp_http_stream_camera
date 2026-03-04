#ifndef _BSP_TOUCH_H_
#define _BSP_TOUCH_H_

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// FT6336 触摸屏配置
#define BSP_TOUCH_I2C_ADDRESS       0x38
#define BSP_TOUCH_INT_GPIO          GPIO_NUM_21  // 中断引脚
#define BSP_TOUCH_RST_GPIO          GPIO_NUM_NC  // 复位引脚（如果没有使用NC）
#define BSP_TOUCH_MAX_POINTS        1            // 最大触摸点数

/**
 * @brief 初始化触摸屏
 * @return 触摸屏句柄，失败返回NULL
 */
lv_indev_t bsp_touch_init(void);

/**
 * @brief 读取触摸点坐标
 * @param x 输出X坐标
 * @param y 输出Y坐标
 * @param pressed 输出是否按下
 * @return ESP_OK成功，ESP_FAIL失败
 */
esp_err_t bsp_touch_read(uint16_t *x, uint16_t *y, bool *pressed);

#ifdef __cplusplus
}
#endif

#endif // _BSP_TOUCH_H_
