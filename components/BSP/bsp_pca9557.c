#include "bsp_pca9557.h"

// PCA9557 设备句柄
i2c_master_dev_handle_t PCA9557_dev_handle = NULL;
static const char *TAG = "PCA9557";

esp_err_t pca9557_dev_init(void){
    if(bsp_i2c_bus_handle==NULL){
        ESP_LOGE("PCA9557", "I2C Bus 未初始化，无法添加设备");
        return ESP_ERR_INVALID_STATE;
    }
    
    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_7,
        .device_address = PCA9557_SENSOR_ADDR,
        .scl_speed_hz = BSP_I2C_FREQ_HZ,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };
    esp_err_t ret = i2c_master_bus_add_device(bsp_i2c_bus_handle, &device_config, &PCA9557_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PCA9557 设备添加失败，错误码: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "检查设备地址是否正确（地址=0x%02X）", PCA9557_SENSOR_ADDR);
    } else {
        ESP_LOGI(TAG, "PCA9557 设备添加成功，句柄: %p", PCA9557_dev_handle);
    }
    return ret;
}


//读取PCA9557寄存器值
esp_err_t pca9557_read_register( uint8_t reg_addr, uint8_t *data, size_t len)
{
    if(bsp_i2c_bus_handle==NULL||len==0||data==NULL){
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(PCA9557_dev_handle,&reg_addr,1,data,len,pdMS_TO_TICKS(1000));
}

//写PCA9557寄存器值
esp_err_t pca9557_write_register( uint8_t reg_addr, uint8_t data)
{
    if(bsp_i2c_bus_handle==NULL){  // 移除data==NULL检查，data是uint8_t不是指针
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t write_buf[2] = {reg_addr, data};
    esp_err_t ret = i2c_master_transmit(PCA9557_dev_handle, write_buf, sizeof(write_buf), pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PCA9557 write reg 0x%02X = 0x%02X failed: %s", reg_addr, data, esp_err_to_name(ret));
    }
    return ret;
}
//初始化PCA9557
void pca9557_init(void){
    //初始化输出寄存器
    pca9557_write_register(PCA9557_OUTPUT_PORT, 0x05); //全部输出低电平
    //配置端口方向寄存器，0为输出，1为输入
    pca9557_write_register(PCA9557_CONFIGURATION_PORT, 0x00); //全部配置为输出
    
}
//设置芯片某个引脚的电平
void pca9557_set_pin(uint8_t pin, uint8_t level){
    uint8_t output_state;
    pca9557_read_register(PCA9557_OUTPUT_PORT, &output_state, 1);
    ESP_LOGI(TAG, "PCA9557 before: 0x%02X, setting pin 0x%02X to %d", output_state, pin, level);
    output_state = SET_BITS(output_state, pin, level); // pin已经是BIT(x)形式，不需要再移位
    pca9557_write_register(PCA9557_OUTPUT_PORT, output_state);
    ESP_LOGI(TAG, "PCA9557 after: 0x%02X", output_state);
}
void bsp_pca9557_dvp_pwdn(uint8_t level){
    pca9557_set_pin(DVP_PWDN_GPIO, level); //PCA9557_GPIO_NUM_2
}
