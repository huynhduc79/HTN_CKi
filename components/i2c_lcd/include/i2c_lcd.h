#ifndef I2C_LCD_H
#define I2C_LCD_H

#include <stdint.h>

void lcd_init(uint8_t addr);
void lcd_set_addr(uint8_t addr);
void lcd_clear(void);
void lcd_goto_xy(uint8_t col, uint8_t row);
void lcd_put_string(const char *str);

#endif