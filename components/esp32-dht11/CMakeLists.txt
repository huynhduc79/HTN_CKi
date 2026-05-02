idf_component_register(SRCS "esp32-dht11.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver log esp_rom freertos)