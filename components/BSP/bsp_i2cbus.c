#include "bsp_i2cbus.h"
#include "esp_log.h"

static const char *TAG = "bsp_i2cbus";

i2c_master_bus_handle_t bsp_i2c_bus_handle = NULL;

esp_err_t bsp_i2c_bus_init()
{
    if (bsp_i2c_bus_handle != NULL) {
        ESP_LOGI(TAG, "I2C bus already initialized, skipping");
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .sda_io_num = BSP_I2C_SDA_GPIO,
        .scl_io_num = BSP_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = 1,
            .allow_pd = 0,
        },
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &bsp_i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Check pin config (SDA=%d, SCL=%d)", BSP_I2C_SDA_GPIO, BSP_I2C_SCL_GPIO);
    } else {
        ESP_LOGI(TAG, "I2C bus initialized successfully (handle: %p, port: I2C_NUM_%d)",
                 bsp_i2c_bus_handle, BSP_I2C_PORT);
    }

    return ret;
}

esp_err_t bsp_i2c_init(void)
{
    return bsp_i2c_bus_init();
}
