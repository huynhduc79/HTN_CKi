#include <string.h>
#include <math.h>               
#include <esp_timer.h>          
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <driver/i2c.h>
#include <rom/ets_sys.h>        

#include <esp_rmaker_standard_types.h>
#include <esp_diagnostics.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>
#include <esp_rmaker_ota.h> // Thư viện cho chức năng cập nhật OTA
#include <app_network.h>
#include <app_insights.h> 

#include "esp32-dht11.h"
#include "i2c_lcd.h"

// --- ĐỊNH NGHĨA CHÂN VÀ ĐỊA CHỈ ---
#define RELAY_1_GPIO 4 // Giữ nguyên chân 4, 5 cho Relay để tránh trùng I2C
#define RELAY_2_GPIO 5

#define DHT11_PIN       0 
#define I2C_SDA_PIN     6
#define I2C_SCL_PIN     7

#define LCD_ADDR_1      0x27 // Địa chỉ LCD 1
#define LCD_ADDR_2      0x26 // Địa chỉ LCD 2 (Đã hàn chân A0)
#define I2C_NUM         I2C_NUM_0

#define ACS1_ADC_CHAN   ADC_CHANNEL_1 // GPIO 1
#define ACS2_ADC_CHAN   ADC_CHANNEL_2 // GPIO 2
#define ZMPT_ADC_CHAN   ADC_CHANNEL_3 // GPIO 3

// --- HỆ SỐ CẢM BIẾN ---
#define ACS712_SENSITIVITY  0.185  // 185 mV/A (Dành cho module 5A)
#define ZMPT101B_CALIBRATION 235.8 // Đã hiệu chuẩn cho mốc 224V

static const char *TAG = "app_main";

typedef struct {
    float temperature;
    float humidity;
    float current_ac_1;
    float current_ac_2;
    float voltage_ac;
    int dht_status;
    bool relay1_state;
    bool relay2_state;
} sensor_data_t;

QueueHandle_t sensor_queue;
SemaphoreHandle_t i2c_mutex;

esp_rmaker_param_t *temp_param, *hum_param, *i1_param, *i2_param, *v_param, *p1_param, *p2_param;
adc_oneshot_unit_handle_t adc1_handle;

// --- CALLBACK XỬ LÝ RELAY ---
static esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
            const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    const char *param_name = esp_rmaker_param_get_name(param);
    
    if (strcmp(param_name, "Relay 1") == 0) {
        gpio_set_level(RELAY_1_GPIO, val.val.b);
        ESP_LOGI(TAG, "Relay 1 set to %d", val.val.b);
    } 
    else if (strcmp(param_name, "Relay 2") == 0) {
        gpio_set_level(RELAY_2_GPIO, val.val.b);
        ESP_LOGI(TAG, "Relay 2 set to %d", val.val.b);
    }
    esp_rmaker_param_update(param, val);
    return ESP_OK;
}

// --- HÀM HIỆU CHUẨN ĐIỆN ÁP TĨNH ---
float calibrate_sensor_offset(adc_oneshot_unit_handle_t handle, adc_channel_t chan) {
    float v_offset = 0;
    int raw_val;
    for (int i = 0; i < 100; i++) {
        adc_oneshot_read(handle, chan, &raw_val);
        v_offset += (raw_val / 4095.0) * 3.3; 
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return v_offset / 100.0;
}

// --- THUẬT TOÁN ĐO AC (TRUE RMS) ---
float read_ac_rms(adc_oneshot_unit_handle_t handle, adc_channel_t chan, float offset, float sens, float cal) {
    int raw;
    float sum_sq = 0;
    int n = 0;
    int64_t start = esp_timer_get_time();
    
    // Lấy mẫu liên tục trong 40ms
    while ((esp_timer_get_time() - start) < 40000) { 
        adc_oneshot_read(handle, chan, &raw);
        float inst = (((raw / 4095.0) * 3.3) - offset) / sens;
        sum_sq += (inst * inst);
        n++;
        ets_delay_us(200);
    }
    
    float rms = sqrt(sum_sq / n) * cal;
    return (rms < 0.05 * cal) ? 0 : rms;
}

// --- TASK SENSOR: ĐỌC VÀ CẬP NHẬT APP 3 GIÂY/LẦN ---
void sensor_task(void *pvParameters) {
    sensor_data_t data;
    dht11_t dht_sensor = { .dht11_pin = DHT11_PIN };

    ESP_LOGI(TAG, "Hieu chuan cac cam bien (Vui long khong cap tai)...");
    float offset_acs1 = calibrate_sensor_offset(adc1_handle, ACS1_ADC_CHAN);
    float offset_acs2 = calibrate_sensor_offset(adc1_handle, ACS2_ADC_CHAN);
    float offset_zmpt = calibrate_sensor_offset(adc1_handle, ZMPT_ADC_CHAN);

    while(1) {
        data.dht_status = dht11_read(&dht_sensor, 15);
        if (data.dht_status == 0) {
            data.temperature = dht_sensor.temperature;
            data.humidity = dht_sensor.humidity;
        }

        data.current_ac_1 = read_ac_rms(adc1_handle, ACS1_ADC_CHAN, offset_acs1, ACS712_SENSITIVITY, 1.0);
        data.current_ac_2 = read_ac_rms(adc1_handle, ACS2_ADC_CHAN, offset_acs2, ACS712_SENSITIVITY, 1.0);
        data.voltage_ac   = read_ac_rms(adc1_handle, ZMPT_ADC_CHAN, offset_zmpt, 1.0, ZMPT101B_CALIBRATION);
        
        data.relay1_state = gpio_get_level(RELAY_1_GPIO);
        data.relay2_state = gpio_get_level(RELAY_2_GPIO);

        xQueueOverwrite(sensor_queue, &data);

        // PUSH TRỰC TIẾP LÊN APP MỖI 3 GIÂY (Không cần vuốt Reload)
        if (data.dht_status == 0) {
            esp_rmaker_param_update_and_report(temp_param, esp_rmaker_float(data.temperature));
            esp_rmaker_param_update_and_report(hum_param, esp_rmaker_float(data.humidity));
        }
        esp_rmaker_param_update_and_report(v_param, esp_rmaker_float(data.voltage_ac));
        esp_rmaker_param_update_and_report(i1_param, esp_rmaker_float(data.current_ac_1));
        esp_rmaker_param_update_and_report(i2_param, esp_rmaker_float(data.current_ac_2));
        
        float p1 = data.voltage_ac * data.current_ac_1;
        float p2 = data.voltage_ac * data.current_ac_2;
        esp_rmaker_param_update_and_report(p1_param, esp_rmaker_float(p1));
        esp_rmaker_param_update_and_report(p2_param, esp_rmaker_float(p2));

        vTaskDelay(pdMS_TO_TICKS(3000)); 
    }
}

// --- TASK HIỂN THỊ 2 LCD ---
void display_task(void *pvParameters) {
    sensor_data_t res;
    char l1[17], l2[17];

    while(1) {
        if (xQueueReceive(sensor_queue, &res, portMAX_DELAY)) {
            if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                
                // --- LCD 1: R1, Nhiệt độ, Áp, Dòng 1, Công suất 1 ---
                lcd_set_addr(LCD_ADDR_1); 
                float p1 = res.voltage_ac * res.current_ac_1;
                sprintf(l1, "R1:%s T:%.1fC", res.relay1_state ? "ON " : "OFF", res.temperature);
                sprintf(l2, "%3.0fV %.1fA %4.0fW", res.voltage_ac, res.current_ac_1, p1);
                
                lcd_goto_xy(0, 0); lcd_put_string(l1);
                lcd_goto_xy(0, 1); lcd_put_string(l2);

                // --- LCD 2: R2, Độ ẩm, Áp, Dòng 2, Công suất 2 ---
                lcd_set_addr(LCD_ADDR_2);
                float p2 = res.voltage_ac * res.current_ac_2;
                sprintf(l1, "R2:%s H:%.1f%%", res.relay2_state ? "ON " : "OFF", res.humidity);
                sprintf(l2, "%3.0fV %.1fA %4.0fW", res.voltage_ac, res.current_ac_2, p2);

                lcd_goto_xy(0, 0); lcd_put_string(l1);
                lcd_goto_xy(0, 1); lcd_put_string(l2);
                
                xSemaphoreGive(i2c_mutex);
            }
        }
    }
}

void app_main() {
    // 1. Reset chân khỏi chức năng JTAG mặc định
    gpio_reset_pin(RELAY_1_GPIO);
    gpio_reset_pin(RELAY_2_GPIO);
    
    // 2. Cấu hình VỪA LÀM OUTPUT VỪA LÀM INPUT (Để hàm gpio_get_level đọc được trạng thái)
    gpio_set_direction(RELAY_1_GPIO, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(RELAY_2_GPIO, GPIO_MODE_INPUT_OUTPUT);
    
    // 3. Đặt trạng thái ban đầu là tắt
    gpio_set_level(RELAY_1_GPIO, 0);
    gpio_set_level(RELAY_2_GPIO, 0);
    
    ESP_ERROR_CHECK(nvs_flash_init());

    // Cài đặt I2C Driver 1 lần
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    i2c_param_config(I2C_NUM, &conf);
    i2c_driver_install(I2C_NUM, conf.mode, 0, 0, 0);

    // Khởi tạo phần cứng 2 LCD
    lcd_init(LCD_ADDR_1); 
    lcd_clear();
    lcd_init(LCD_ADDR_2); 
    lcd_clear();

    // Cấu hình ADC (Chỉ cấu hình chân 1, 2, 3 - Bỏ qua chân 0)
    adc_oneshot_unit_init_cfg_t i_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&i_cfg, &adc1_handle));
    adc_oneshot_chan_cfg_t c_cfg = { .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12 };
    adc_oneshot_config_channel(adc1_handle, ACS1_ADC_CHAN, &c_cfg);
    adc_oneshot_config_channel(adc1_handle, ACS2_ADC_CHAN, &c_cfg);
    adc_oneshot_config_channel(adc1_handle, ZMPT_ADC_CHAN, &c_cfg);

    sensor_queue = xQueueCreate(1, sizeof(sensor_data_t));
    i2c_mutex = xSemaphoreCreateMutex();

    // Init RainMaker
    app_network_init();
    esp_rmaker_config_t rmaker_cfg = { .enable_time_sync = false };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rmaker_cfg, "DUT-Embedded", "Smart-Power");

    // TẠO RELAY MODULE (Dạng nút gạt - UI Toggle)
    esp_rmaker_device_t *relay_dev = esp_rmaker_device_create("Relay Module", NULL, NULL);
    esp_rmaker_device_add_cb(relay_dev, write_cb, NULL);
    
    esp_rmaker_param_t *r1_param = esp_rmaker_param_create("Relay 1", ESP_RMAKER_PARAM_POWER, esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(r1_param, ESP_RMAKER_UI_TOGGLE);
    esp_rmaker_device_add_param(relay_dev, r1_param);

    esp_rmaker_param_t *r2_param = esp_rmaker_param_create("Relay 2", ESP_RMAKER_PARAM_POWER, esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(r2_param, ESP_RMAKER_UI_TOGGLE);
    esp_rmaker_device_add_param(relay_dev, r2_param);
    
    esp_rmaker_node_add_device(node, relay_dev);

    // TẠO SENSOR MODULE
    esp_rmaker_device_t *sensor_dev = esp_rmaker_device_create("Sensor Module", NULL, NULL);
    
    temp_param = esp_rmaker_param_create("Temperature", NULL, esp_rmaker_float(0.0), PROP_FLAG_READ);
    hum_param  = esp_rmaker_param_create("Humidity", NULL, esp_rmaker_float(0.0), PROP_FLAG_READ);
    v_param    = esp_rmaker_param_create("Voltage", NULL, esp_rmaker_float(0.0), PROP_FLAG_READ);
    i1_param   = esp_rmaker_param_create("Current 1", NULL, esp_rmaker_float(0.0), PROP_FLAG_READ);
    p1_param   = esp_rmaker_param_create("Power 1", NULL, esp_rmaker_float(0.0), PROP_FLAG_READ);
    i2_param   = esp_rmaker_param_create("Current 2", NULL, esp_rmaker_float(0.0), PROP_FLAG_READ);
    p2_param   = esp_rmaker_param_create("Power 2", NULL, esp_rmaker_float(0.0), PROP_FLAG_READ);
    
    esp_rmaker_device_add_param(sensor_dev, temp_param);
    esp_rmaker_device_add_param(sensor_dev, hum_param);
    esp_rmaker_device_add_param(sensor_dev, v_param);
    esp_rmaker_device_add_param(sensor_dev, i1_param);
    esp_rmaker_device_add_param(sensor_dev, p1_param);
    esp_rmaker_device_add_param(sensor_dev, i2_param);
    esp_rmaker_device_add_param(sensor_dev, p2_param);
    
    esp_rmaker_node_add_device(node, sensor_dev);

    app_insights_enable();

    // KÍCH HOẠT CHỨC NĂNG CẬP NHẬT OTA QUA WIFI
    esp_rmaker_ota_enable_default();

    esp_rmaker_start();
    
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
    xTaskCreate(display_task, "display_task", 4096, NULL, 6, NULL);
    
    app_network_start(POP_TYPE_RANDOM);
}