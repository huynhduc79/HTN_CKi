# Chỉ khai báo các file .c đang thực sự nằm trong thư mục main
idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_adc esp32-dht11 esp_timer nvs_flash i2c_lcd esp_diagnostics)