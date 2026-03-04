set(srcs  "bsp_lcd.c" "bsp_camera.c" "bsp_pca9557.c" "bsp_i2cbus.c" "bsp_lvgl.c" "bsp_touch.c" "bsp_ui.c" "myfont/lv_font_siyuanbold_jibenhanzi_16.c")

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "inc"
    REQUIRES esp_lcd esp32-camera driver esp_timer freertos lvgl__lvgl esp_lcd_touch_ft5x06 esp_http_client json esp_netif esp-tls
)
