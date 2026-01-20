#ifndef _ST7789_H_
#define _ST7789_H_

#include <stdint.h>

#define TFT_W 240
#define TFT_H 240

void st7789_setup(uint32_t spiSpeed);
void test_st7789();
void tft_fill(uint16_t color);
void tft_fill_rect(uint16_t beg_line,uint16_t beg_col,uint16_t lines_nb,uint16_t col_nb,uint16_t color);
void tft_draw_text_12x12_block(uint16_t x,uint16_t y,const char *s,uint16_t fg,uint16_t bg);
void tft_draw_text_12x12_block_(uint16_t x,uint16_t y,const char *s,uint16_t fg,uint16_t bg,int8_t mult);
#ifdef GLOBAL_DMA_IRQ_HANDLER 
void st_dma_irq_handler();
#endif
#endif //_ST7789_H_