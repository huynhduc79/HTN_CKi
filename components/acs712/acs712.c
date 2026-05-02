#include "acs712.h"
#include "esp_log.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h" // THÊM DÒNG NÀY
#include "freertos/task.h"     // THÊM DÒNG NÀY

static const char *TAG = "ACS712";

esp_err_t acs712_init(acs712_config_t *config) {
    // Cấu hình channel ADC
    adc_oneshot_chan_cfg_t config_chan = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, // Đọc dải điện áp lên đến ~3.1V
    };
    return adc_oneshot_config_channel(config->adc_handle, config->channel, &config_chan);
}

void acs712_calibrate(acs712_config_t *config) {
    ESP_LOGI(TAG, "Dang hieu chuan... Vui long khong tai!");
    int raw_sum = 0;
    int samples = 100;
    
    for (int i = 0; i < samples; i++) {
        int raw;
        adc_oneshot_read(config->adc_handle, config->channel, &raw);
        raw_sum += raw;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    config->adc_raw_offset = raw_sum / samples;
    ESP_LOGI(TAG, "Hieu chuan xong. Offset thue: %d", config->adc_raw_offset);
}

float acs712_get_current(acs712_config_t *config) {
    int raw;
    adc_oneshot_read(config->adc_handle, config->channel, &raw);
    
    // Tính toán dựa trên độ lệch so với điểm 0A
    // Giả sử ESP32-C3 ADC 12-bit (0-4095) tương ứng 0-3100mV (ở 12dB)
    float voltage = (float)(raw - config->adc_raw_offset) * (3100.0 / 4095.0);
    
    // Dòng điện (A) = Điện áp dư (mV) / Độ nhạy (mV/A)
    return voltage / config->sensitivity;
    if (current < 0.01 && current > -0.01) {
        current = 0.0;
    }
}