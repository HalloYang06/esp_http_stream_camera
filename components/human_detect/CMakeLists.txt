idf_component_register(SRCS "detection.c" "middle_human_detect.cpp"
                    INCLUDE_DIRS "inc" 
                    REQUIRES esp_http_client esp-tls esp_netif json esp_timer freertos esp32-camera human_face_detect pedestrian_detect
                    )
target_compile_options(${COMPONENT_LIB} PRIVATE -fexceptions)