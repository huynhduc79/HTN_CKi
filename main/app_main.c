#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <driver/gpio.h>

// 1. Thêm các thư viện còn thiếu
#include <esp_rmaker_standard_types.h> // Để dùng ESP_RMAKER_UI_TOGGLE và ESP_RMAKER_DEVICE_SENSOR
#include <esp_diagnostics.h>           // Để dùng ESP_DIAG_EVENT

// 2. Thư viện ADC mới (Chuẩn ESP-IDF v5+)
#include <esp_adc/adc_oneshot.h>

#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>
#include <app_network.h>
#include <app_insights.h> 

#define RELAY_1_GPIO 4
#define RELAY_2_GPIO 5

static const char *TAG = "app_main";

typedef struct {
    int sensor1_val;
    int sensor2_val;
    int sensor3_val;
} sensor_data_t;

QueueHandle_t sensor_queue;
SemaphoreHandle_t lcd_mutex;

esp_rmaker_param_t *s1_param, *s2_param, *s3_param;

// Khai báo Handle cho bộ ADC mới
adc_oneshot_unit_handle_t adc1_handle;

static esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
            const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    const char *param_name = esp_rmaker_param_get_name(param);
    
    if (strcmp(param_name, "Relay 1") == 0) {
        gpio_set_level(RELAY_1_GPIO, val.val.b);
        esp_rmaker_param_update(param, val);
        ESP_LOGI(TAG, "Relay 1 set to %d", val.val.b);
        ESP_DIAG_EVENT("RELAY_CTRL", "Relay 1 turned %s", val.val.b ? "ON" : "OFF");
    } 
    else if (strcmp(param_name, "Relay 2") == 0) {
        gpio_set_level(RELAY_2_GPIO, val.val.b);
        esp_rmaker_param_update(param, val);
        ESP_LOGI(TAG, "Relay 2 set to %d", val.val.b);
        ESP_DIAG_EVENT("RELAY_CTRL", "Relay 2 turned %s", val.val.b ? "ON" : "OFF");
    }
    return ESP_OK;
}

void sensor_task(void *pvParameters) {
    sensor_data_t data;
    while(1) {
        // Đọc 3 kênh ADC bằng API mới của ESP-IDF v5
        adc_oneshot_read(adc1_handle, ADC_CHANNEL_0, &data.sensor1_val);
        adc_oneshot_read(adc1_handle, ADC_CHANNEL_1, &data.sensor2_val);
        adc_oneshot_read(adc1_handle, ADC_CHANNEL_2, &data.sensor3_val);

        xQueueOverwrite(sensor_queue, &data);

        esp_rmaker_param_update_and_report(s1_param, esp_rmaker_int(data.sensor1_val));
        esp_rmaker_param_update_and_report(s2_param, esp_rmaker_int(data.sensor2_val));
        esp_rmaker_param_update_and_report(s3_param, esp_rmaker_int(data.sensor3_val));

        vTaskDelay(pdMS_TO_TICKS(2000)); 
    }
}

void display_task(void *pvParameters) {
    sensor_data_t received_data;
    while(1) {
        if (xQueueReceive(sensor_queue, &received_data, portMAX_DELAY)) {
            if (xSemaphoreTake(lcd_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                ESP_LOGI("LCD_TASK", "Hien thi LCD: %d, %d, %d", 
                         received_data.sensor1_val, received_data.sensor2_val, received_data.sensor3_val);
                xSemaphoreGive(lcd_mutex);
            }
        }
    }
}

void app_main() {
    gpio_set_direction(RELAY_1_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(RELAY_2_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(RELAY_1_GPIO, 0);
    gpio_set_level(RELAY_2_GPIO, 0);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // --- Khởi tạo ADC theo chuẩn ESP-IDF v5+ ---
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1, // Sử dụng bộ ADC 1
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, // Dải đo 0 - 3.3V
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_0, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_1, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_2, &config));
    // -------------------------------------------

    sensor_queue = xQueueCreate(1, sizeof(sensor_data_t));
    lcd_mutex = xSemaphoreCreateMutex();

    app_network_init();
    esp_rmaker_config_t rainmaker_cfg = { .enable_time_sync = false };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "ESP32-C3 Node", "Multi-Device");

    esp_rmaker_device_t *relay_dev = esp_rmaker_device_create("Relay Module", NULL, NULL);
    esp_rmaker_device_add_cb(relay_dev, write_cb, NULL);
    
    esp_rmaker_param_t *r1_param = esp_rmaker_param_create("Relay 1", NULL, esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(r1_param, ESP_RMAKER_UI_TOGGLE);
    esp_rmaker_device_add_param(relay_dev, r1_param);

    esp_rmaker_param_t *r2_param = esp_rmaker_param_create("Relay 2", NULL, esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(r2_param, ESP_RMAKER_UI_TOGGLE);
    esp_rmaker_device_add_param(relay_dev, r2_param);
    
    esp_rmaker_node_add_device(node, relay_dev);

    esp_rmaker_device_t *sensor_dev = esp_rmaker_device_create("Sensor Module", ESP_RMAKER_DEVICE_SPEAKER, NULL);
    
    s1_param = esp_rmaker_param_create("ADC 1", NULL, esp_rmaker_int(0), PROP_FLAG_READ);
    s2_param = esp_rmaker_param_create("ADC 2", NULL, esp_rmaker_int(0), PROP_FLAG_READ);
    s3_param = esp_rmaker_param_create("ADC 3", NULL, esp_rmaker_int(0), PROP_FLAG_READ);
    
    esp_rmaker_device_add_param(sensor_dev, s1_param);
    esp_rmaker_device_add_param(sensor_dev, s2_param);
    esp_rmaker_device_add_param(sensor_dev, s3_param);
    
    esp_rmaker_node_add_device(node, sensor_dev);

    app_insights_enable();
    esp_rmaker_start();

    xTaskCreate(sensor_task, "Sensor Task", 4096, NULL, 5, NULL);
    xTaskCreate(display_task, "Display Task", 4096, NULL, 6, NULL);

    app_network_start(POP_TYPE_RANDOM);
}