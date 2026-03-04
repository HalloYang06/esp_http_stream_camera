#ifndef _BSP_LVGL_H_
#define _BSP_LVGL_H_

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 LVGL 核心和显示驱动
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t bsp_lvgl_init(void);

/**
 * @brief 启动 LVGL tick 和 timer 任务
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t bsp_lvgl_start_tasks(void);

/**
 * @brief 获取 LVGL 显示驱动句柄
 * @return 显示驱动句柄
 */
lv_display_t* bsp_lvgl_get_display(void);

#ifdef __cplusplus
}
#endif

#endif // _BSP_LVGL_H_
