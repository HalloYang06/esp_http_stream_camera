idf_component_register(SRCS "network.c" "main.c" "esp_cam.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver freertos
                    esp_lcd
                    esp_http_server
                    esp_http_client
                    esp_netif
                    esp_wifi
                    nvs_flash)

# 控制LCD显示任务是否启用
# 取消下面这行注释以启用LCD显示，注释掉则禁用LCD显示
 target_compile_definitions(${COMPONENT_LIB} PRIVATE LCD_ON)
