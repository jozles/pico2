#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "const.h"
#include "std_utils.h"
#include "screen.h"

#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "hardware/irq.h"

#ifdef ST7789
#include "st7789.h"
#else
#include "display_esp.h"
#endif

extern uint32_t millisCounter;
uint32_t ticker10=0;

// ---------------------------------------------------------
// shared by both display modes (ST7789 or ESP32 link)
// ---------------------------------------------------------
int                disp_dma_chan = -1;
dma_channel_config disp_dma_cfg;
spin_lock_t*       disp_dma_lock;

int disp_dma_init()
{
    disp_dma_chan = dma_claim_unused_channel(false);    // false: return -1 instead of halting
    if (disp_dma_chan < 0) { return -1; }

    disp_dma_cfg = dma_channel_get_default_config(disp_dma_chan);
    channel_config_set_transfer_data_size(&disp_dma_cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&disp_dma_cfg, true);
    channel_config_set_write_increment(&disp_dma_cfg, false);
    channel_config_set_dreq(&disp_dma_cfg, DREQ_SPI0_TX);

    dma_channel_set_irq1_enabled(disp_dma_chan, true);
#ifndef GLOBAL_DMA_IRQ_HANDLER
    irq_set_exclusive_handler(DMA_IRQ_1, disp_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);
#endif
    return disp_dma_chan;
}

void __not_in_flash_func(disp_dma_irq_handler)()
{
    while (spi_is_busy(spi0)) {             // the last bytes are still being shifted out
        tight_loop_contents();
    }
#ifdef ST7789
    st_dma_end();
#else
    esp_dma_end();
#endif
    dma_hw->ints0 = 1u << disp_dma_chan;    // clear the channel flag (INTS0 and INTS1 clear the same raw flag)
}

int display_setup(uint32_t spiSpeed)
{
    gpio_init(DISP_PIN_CS); gpio_set_dir(DISP_PIN_CS, GPIO_OUT);
    gpio_put(DISP_PIN_CS, 1);

    uint32_t real = spi_init(spi0, spiSpeed);   // returns the frequency actually obtained
    gpio_set_function(DISP_PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(DISP_PIN_MOSI, GPIO_FUNC_SPI);
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    printf("display_setup: SPI %lu Hz\n", (unsigned long)real);

    disp_dma_lock = spin_lock_init(DMA_LOCK);
    if (disp_dma_init() < 0) { printf("display_setup: no SPI DMA channel available\n"); return -1; }

#ifdef ST7789
    if (st7789_setup() < 0) { return -1; }
#else
    if (display_esp_setup() < 0) { return -1; }
#endif
    return disp_dma_chan;
}

#ifndef ST7789
void tft_fill_rect_blank(uint16_t beg_line,uint16_t beg_col,uint16_t lines_nb,uint16_t col_nb)
{
    lnk_clear(beg_col, beg_line, col_nb, lines_nb, COL_BLACK);
}
#endif

void display(uint16_t x,uint16_t y,const char *s,uint16_t fg,uint16_t bg,int8_t mult)
{
#ifdef ST7789
    tft_draw_text_12x12_dma_mult( x, y, s, fg, bg, mult);
#else
    lnk_display(x, y, s, fg, bg, mult);
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
#else
    lnk_display(x, y, s, fg, bg, mult);     // the ESP has a single font
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
