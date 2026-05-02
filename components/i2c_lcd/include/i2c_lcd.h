#ifndef I2C_LCD_H
#define I2C_LCD_H

#include "driver/i2c.h"

// Hàm kh?i t?o màn h?nh (Ð?a ch? I2C thý?ng là 0x27 ho?c 0x3F)
void lcd_init(uint8_t addr, int sda_pin, int scl_pin);

// Hàm in 1 chu?i k? t? ra màn h?nh
void lcd_put_string(const char *str);

// Hàm xóa toàn b? màn h?nh
void lcd_clear(void);

// Hàm di chuy?n con tr? (col: 0-15, row: 0-1)
void lcd_goto_xy(uint8_t col, uint8_t row);

#endif
