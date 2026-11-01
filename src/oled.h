#pragma once
void oled_init(void);
void oled_clear(void);
void oled_text(int row, const char *s);   // 8 rows x 21 chars
void oled_invert_row(int row);
void oled_flush(void);
