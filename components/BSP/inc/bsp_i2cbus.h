#ifndef _BSP_I2CBUS_H_
#define _BSP_I2CBUS_H_

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/****************   I2C Bus Configuration   ***********************************/
#define BSP_I2C_SCL_GPIO      (GPIO_NUM_2)
#define BSP_I2C_SDA_GPIO      (GPIO_NUM_1)
#define BSP_I2C_FREQ_HZ       (100000)
#define BSP_I2C_PORT          (I2C_NUM_0)

/**
 * @brief I2C master bus handle (shared by all I2C devices)
 * @note This handle is used by:
 *       - bsp_pca9557: IO expander device
 *       - bsp_camera: SCCB interface (sensor communication)
 *       Call bsp_i2c_bus_init() to initialize this handle before using dependent modules
 */
extern i2c_master_bus_handle_t bsp_i2c_bus_handle;

/**
 * @brief Initialize I2C bus
 * @note This function is idempotent - safe to call multiple times
 * @return ESP_OK on success
 */
esp_err_t bsp_i2c_bus_init(void);

/**
 * @brief Compatibility alias for bsp_i2c_bus_init
 */
esp_err_t bsp_i2c_init(void);

#ifdef __cplusplus
}
#endif

#endif // _BSP_I2CBUS_H_
