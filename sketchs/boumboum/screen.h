#ifndef _SCREEN_H
#define _SCREEN_H


void display(uint16_t x,uint16_t y,const char *s,uint16_t fg,uint16_t bg,int8_t mult);
void display11(uint16_t x,uint16_t y,const char *s,uint16_t fg,uint16_t bg,int8_t mult);
void scope(int32_t* buf,float f,uint16_t begline,bool fd,bool blk,uint8_t refr,uint8_t wf,uint8_t mode_calcul,uint8_t object,int16_t zline);
void tft_fill_rect_blank(uint16_t beg_line,uint16_t beg_col,uint16_t lines_nb,uint16_t col_nb);
bool debug_ticker();

int  display_setup(uint32_t spiSpeed);
int  disp_dma_init();
void disp_dma_irq_handler();

#endif  // _SCREEN_H
