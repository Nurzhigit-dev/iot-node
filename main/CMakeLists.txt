idf_component_register(SRCS "main.c" "bmp180.c" "wifi.c" "buffer.c" "ota.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_i2c esp_wifi esp_netif esp_event nvs_flash
                                     esp_timer spiffs esp_https_ota esp_http_client
                                     app_update esp_app_format)