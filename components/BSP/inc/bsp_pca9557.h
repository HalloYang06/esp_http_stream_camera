#ifndef __BSP_PCA9557_H_
#define __BSP_PCA9557_H_

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "bsp_i2cbus.h"
#define PCA9557_INPUT_PORT              0x00
#define PCA9557_OUTPUT_PORT             0x01
#define PCA9557_POLARITY_INVERSION_PORT 0x02
#define PCA9557_CONFIGURATION_PORT      0x03

#define LCD_CS_GPIO                 BIT(0)    // PCA9557_GPIO_NUM_1
#define PA_EN_GPIO                  BIT(1)    // PCA9557_GPIO_NUM_2
#define DVP_PWDN_GPIO               BIT(2)    // PCA9557_GPIO_NUM_3

#define PCA9557_SENSOR_ADDR             0x19        /*!< Slave address of the MPU9250 sensor */

#define SET_BITS(_m, _s, _v)  ((_v) ? (_m)|((_s)) : (_m)&~((_s)))
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t pca9557_dev_init(void);
void lcd_cs(uint8_t level);
void pa_en(uint8_t level);
void dvp_pwdn(uint8_t level);
void pca9557_init(void);
void pca9557_set_pin(uint8_t pin, uint8_t level);
esp_err_t pca9557_write_register( uint8_t reg_addr, uint8_t data);
void bsp_pca9557_dvp_pwdn(uint8_t level);
#ifdef __cplusplus
}   
#endif 
#endif