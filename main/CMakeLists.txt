idf_component_register(SRCS
                    "main.c"
                    "wifi_config.c"
                    "wifi_manager.c"
                    "http_server.c"
                    "config_server.c"
                    "mdns_service.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver freertos
                    esp_lcd
                    esp_http_server
                    esp_http_client
                    esp_netif
                    esp_wifi
                    nvs_flash
                    mdns
                    BSP
                    lvgl__lvgl
                    )

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format)