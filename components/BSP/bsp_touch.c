#include "bsp_touch.h"
#include "bsp_i2cbus.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "bsp_touch";
static lv_indev_t touch_handle = NULL;

lv_indev_t bsp_touch_init(void)
{
    if (touch_handle != NULL) {
        ESP_LOGW(TAG, "Touch already initialized");
        return touch_handle;
    }

    // 配置中断引脚（可选）
    if (BSP_TOUCH_INT_GPIO != GPIO_NUM_NC) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << BSP_TOUCH_INT_GPIO),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
    }

    // 配置复位引脚（可选）
    if (BSP_TOUCH_RST_GPIO != GPIO_NUM_NC) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << BSP_TOUCH_RST_GPIO),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);

        // 复位触摸芯片
        gpio_set_level(BSP_TOUCH_RST_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(BSP_TOUCH_RST_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // 获取I2C总线句柄
    i2c_master_bus_handle_t i2c_bus = bsp_i2c_bus_handle;
    if (i2c_bus == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return NULL;
    }

    // 配置触摸屏
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    tp_io_config.scl_speed_hz = 400000;  // 400kHz

    esp_err_t ret = esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_config, &tp_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create touch panel IO: %s", esp_err_to_name(ret));
        return NULL;
    }

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = 320,
        .y_max = 240,
        .rst_gpio_num = BSP_TOUCH_RST_GPIO,
        .int_gpio_num = BSP_TOUCH_INT_GPIO,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 1,
            .mirror_y = 0,
        },
    };

    ret = esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create touch handle: %s", esp_err_to_name(ret));
        return NULL;
    }

    ESP_LOGI(TAG, "Touch initialized successfully (FT6336)");
    esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &touch_handle);
    return touch_handle;
}

esp_err_t bsp_touch_read(uint16_t *x, uint16_t *y, bool *pressed)
{
    if (touch_handle == NULL) {
        return ESP_FAIL;
    }

    uint16_t touch_x[1];
    uint16_t touch_y[1];
    uint16_t touch_strength[1];
    uint8_t touch_cnt = 0;

    esp_err_t ret = esp_lcd_touch_read_data(touch_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    bool touch_pressed = esp_lcd_touch_get_coordinates(touch_handle, touch_x, touch_y, touch_strength, &touch_cnt, 1);

    if (touch_pressed && touch_cnt > 0) {
        *x = touch_x[0];
        *y = touch_y[0];
        *pressed = true;
    } else {
        *pressed = false;
    }

    return ESP_OK;
}
