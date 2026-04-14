#ifndef _BSP_UI_H_
#define _BSP_UI_H_

#include "esp_err.h"
#include "lvgl.h"
#include "bsp_lcd.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 UI 界面
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t bsp_ui_init(void);

/**
 * @brief 更新识别结果显示
 * @param text 要显示的文本
 */
void bsp_ui_update_result(const char *text);

/**
 * @brief 更新状态显示
 * @param status 状态文本
 */
void bsp_ui_update_status(const char *status);

#ifdef __cplusplus
}
#endif

#endif // _BSP_UI_H_
