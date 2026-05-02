#ifndef ACS712_H
#define ACS712_H

#include "esp_adc/adc_oneshot.h"

// Định nghĩa độ nhạy của từng loại cảm biến (mV/A)
#define ACS712_SENS_5A  185
#define ACS712_SENS_20A 100
#define ACS712_SENS_30A 66

typedef struct {
    adc_oneshot_unit_handle_t adc_handle;
    adc_channel_t channel;
    int sensitivity;      // mV/A
    int v_offset;         // Điện áp tại 0A (thường là 2500mV)
    int adc_raw_offset;   // Giá trị ADC thô khi không có dòng điện
} acs712_config_t;

// Khởi tạo cảm biến
esp_err_t acs712_init(acs712_config_t *config);

// Hàm hiệu chuẩn để tìm điểm 0A (Zero Calibration)
void acs712_calibrate(acs712_config_t *config);

// Đọc giá trị dòng điện (Ampe)
float acs712_get_current(acs712_config_t *config);

#endif