#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "const.h"
#include "std_utils.h"

#ifdef ST7789
#include "st7789.h"
#endif

extern uint32_t millisCounter;
uint32_t ticker10=0;


#ifndef ST7789
void tft_fill_rect_blank(uint16_t beg_line,uint16_t beg_col,uint16_t lines_nb,uint16_t col_nb)
{

}
#endif

void display(uint16_t x,uint16_t y,const char *s,uint16_t fg,uint16_t bg,int8_t mult)
{
#ifdef ST7789
    tft_draw_text_12x12_dma_mult( x, y, s, fg, bg, mult);
#endif
#ifndef ST7789
#endif    

}

uint16_t dispint(uint16_t x,uint16_t y,uint16_t fg,uint16_t bg,int8_t mult,int32_t num,uint8_t len){
    #define MAXL 32
    char st[MAXL];
    memset(st,0x00,MAXL);
    uint8_t l=convIntToString(st,num);
    if(len>l && len!=0){memset(st+l,' ',len-l);}
    display(x,y,st,fg,bg,mult);
    return strstr(st,"\0")-st;
}

uint16_t dispint(uint16_t x,uint16_t y,uint16_t fg,uint16_t bg,int8_t mult,int32_t num){
    return dispint(x,y,fg,bg,mult,num,0);
}

uint16_t dispfloat(uint16_t x,uint16_t y,uint16_t fg,uint16_t bg,int8_t mult,float num,uint8_t len){
    #define MAXL 32
    char st[MAXL];
    memset(st,0x00,MAXL);
    uint8_t l=convNumToString(st,num);
    if(len>l && len!=0){memset(st+l,' ',len-l);}
    display(x,y,st,fg,bg,mult);
    return strstr(st,"\0")-st;
}

uint16_t dispfloat(uint16_t x,uint16_t y,uint16_t fg,uint16_t bg,int8_t mult,float num){
    return dispfloat(x,y,fg,bg,mult,num,0);
}

void display11(uint16_t x,uint16_t y,const char *s,uint16_t fg,uint16_t bg,int8_t mult)
{
#ifdef ST7789
    tft_draw_text_11x12_dma_mult( x, y, s, fg, bg, mult);
#endif
#ifndef ST7789
#endif    

}

bool debug_ticker(){
    if(__builtin_expect((millisCounter-ticker10)>10000,0)){
        printf("10s_cnt:%d\n",millisCounter/10000);ticker10=millisCounter;
        
        // printf("W v2 f:%u att:%u amp:%u | lfm0 c:%u %u %u %u | lfm1 c:%u %u %u %u\n",
        //voices[2].coderFreq, voices[2].coderFreqAtt, voices[2].coderWaveAmpl[0],
        //lfmCoder[0][0],lfmCoder[1][0],lfmCoder[2][0],lfmCoder[3][0],
        //lfmCoder[0][1],lfmCoder[1][1],lfmCoder[2][1],lfmCoder[3][1]);
        
        dispint(TFT_W-4*12,0,0x001f,0x0000,1,millisCounter/10000); 
        return true;   
    }
    return false;
}

#ifndef ST7789
void scope(int32_t* buf,float f,uint16_t begline,bool fd,bool blk,uint8_t refr,uint8_t wf,uint8_t mode_calcul,uint8_t object,int16_t zline)
{
    
}
#endif
