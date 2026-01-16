#ifndef _ST7789_H_
#define _ST7789_H_  

#include <stdint.h>

void st7789_Setup(uint32_t spi_speed_hz);
void tft_fill_rect(uint16_t line_debut,uint16_t colonne_debut,uint16_t nbre_lignes,uint16_t nbre_colonnes,uint16_t color);
void tft_draw_char_12x12(uint16_t y, uint16_t x,char c,uint16_t color_fg,uint16_t color_bg);
void tft_draw_text_12x12(uint16_t y, uint16_t x,const char *s,uint16_t color_fg,uint16_t color_bg);
void tft_draw_text_12x12_block(uint16_t x,uint16_t y,const char *s,uint16_t fg,uint16_t bg);
void tft_draw_text_12x12_block_(uint16_t x,uint16_t y,const char *s,uint16_t fg,uint16_t bg,int8_t mult);
void test_st7789();


#endif //_ST7789_H_