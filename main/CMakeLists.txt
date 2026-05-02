idf_component_register(SRCS "app_driver.c" 
                            "app_main.c"
                            "acs712.c"
                            "esp32-dht11.c"       # Sửa lại nếu file của bạn tên là esp32-dht11.c
                            "i2c_lcd.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_adc)