set(srcs  "bsp_lcd.c" "bsp_camera.c" "bsp_pca9557.c" "bsp_i2cbus.c" "bsp_lvgl.c" )

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "inc"
    REQUIRES esp_lcd esp32-camera driver esp_timer freertos lvgl__lvgl
)
