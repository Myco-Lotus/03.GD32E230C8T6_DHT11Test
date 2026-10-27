#ifndef OLED_H
#define OLED_H
#include <stdint.h>

void oled_init(void);
void oled_clear(void);
void oled_show_hanzi(uint8_t idx, uint8_t x, uint8_t page);   /* 汉字 16x16，占 2 页 */
void oled_show_str(uint8_t x, uint8_t page, const char *s);   /* ASCII 8x16，占 2 页 */

#endif /* OLED_H */
