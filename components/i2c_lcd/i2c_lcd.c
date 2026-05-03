#include "i2c_lcd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "rom/ets_sys.h" 
#include "driver/i2c.h" // Thêm thư viện I2C gốc của ESP-IDF

#define I2C_NUM I2C_NUM_0
static uint8_t lcd_addr;

#define PIN_RS    (1 << 0)
#define PIN_EN    (1 << 2)
#define BACKLIGHT (1 << 3)

static esp_err_t i2c_write_byte(uint8_t val) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (lcd_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static void lcd_write_nibble(uint8_t nibble, uint8_t mode) {
    uint8_t data = (nibble & 0xF0) | mode | BACKLIGHT;
    i2c_write_byte(data | PIN_EN);  
    ets_delay_us(1);                
    i2c_write_byte(data & ~PIN_EN); 
    ets_delay_us(40);               
}

static void lcd_send_cmd(uint8_t cmd) {
    lcd_write_nibble(cmd & 0xF0, 0);    
    lcd_write_nibble(cmd << 4, 0);      
}

static void lcd_send_data(uint8_t data) {
    lcd_write_nibble(data & 0xF0, PIN_RS); 
    lcd_write_nibble(data << 4, PIN_RS);   
}

void lcd_clear(void) {
    lcd_send_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(2));
}

void lcd_goto_xy(uint8_t col, uint8_t row) {
    uint8_t row_offsets[] = {0x00, 0x40};
    lcd_send_cmd(0x80 | (col + row_offsets[row]));
}

void lcd_put_string(const char *str) {
    while (*str) lcd_send_data(*str++);
}

// --- HÀM MỚI: Chỉ đổi địa chỉ I2C đang trỏ tới ---
void lcd_set_addr(uint8_t addr) {
    lcd_addr = addr;
}

// --- HÀM INIT: Chỉ khởi tạo phần cứng màn hình (không cài I2C nữa) ---
void lcd_init(uint8_t addr) {
    lcd_addr = addr;

    vTaskDelay(pdMS_TO_TICKS(50)); // Chờ nguồn ổn định

    // Ép về chế độ 8-bit 3 lần
    lcd_write_nibble(0x30, 0); 
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write_nibble(0x30, 0); 
    ets_delay_us(150);
    lcd_write_nibble(0x30, 0); 
    vTaskDelay(pdMS_TO_TICKS(1));

    // Chuyển sang 4-bit
    lcd_write_nibble(0x20, 0); 
    vTaskDelay(pdMS_TO_TICKS(1));

    lcd_send_cmd(0x28); // 4-bit, 2 lines, 5x8
    lcd_send_cmd(0x08); // Display OFF
    lcd_send_cmd(0x01); // Clear Display
    vTaskDelay(pdMS_TO_TICKS(2));
    lcd_send_cmd(0x06); // Tự tăng con trỏ
    lcd_send_cmd(0x0C); // Display ON, Cursor OFF
}