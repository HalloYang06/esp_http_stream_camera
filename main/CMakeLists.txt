idf_component_register(SRCS "network.c" "main.c" "esp_cam.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver freertos
                    esp_lcd
                    esp_http_server
                    esp_http_client
                    esp_netif
                    esp_wifi
                    nvs_flash)

