#ifndef _ST7789_H_
#define _ST7789_H_

#include <stdint.h>

#define TFT_W 240
#define TFT_H 240

int st7789_setup(uint32_t spiSpeed);
volatile bool get_st_dma_done();        // uniquement à des fins de consultation
void test_st7789(uint32_t ms);
void tft_fill(uint16_t color);
void tft_fill_rect(uint16_t beg_line,uint16_t beg_col,uint16_t lines_nb,uint16_t col_nb,uint16_t color);
void tft_draw_text_12x12_block(uint16_t x,uint16_t y,const char *s,uint16_t fg,uint16_t bg);
void tft_draw_text_12x12_dma_mult(uint16_t x,uint16_t y,const char *s,uint16_t fg,uint16_t bg,int8_t mult);
uint16_t tft_draw_int_12x12_dma_mult(uint16_t x,uint16_t y,uint16_t fg,uint16_t bg,int8_t mult,int32_t num);
uint16_t tft_draw_float_12x12_dma_mult(uint16_t x,uint16_t y,uint16_t fg,uint16_t bg,int8_t mult,float num);
#ifdef GLOBAL_DMA_IRQ_HANDLER 
void st_dma_irq_handler();
#endif
#endif //_ST7789_H_