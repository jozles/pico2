/* st7789 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "st7789.h"
#include "st7789_fonts.h"
#include "frequences.h"

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "hardware/sync.h"

#include "font12x12.h"
#include "font11x12.h"

#include "const.h"
#include "util.h"

uint32_t ticker10=0;

extern uint32_t millisCounter;
extern uint32_t st_dma_tfr_count;

static int st_dma_chan=-1;
static dma_channel_config dma_cfg;
static dma_channel_config dma_cfg_single;
static const uint16_t zero16=0x0000;
//static 
volatile bool st_dma_free = true;      // false busy : dma en cours sur buffer
static spin_lock_t *st_dma_lock;
//static 
volatile bool st_dma_done_blank = false;
//static 
volatile bool st_sched_free = true;
static uint8_t* sched_frame;
static size_t sched_size;
//static 
volatile bool st_buffer_free = true; // false busy : load buffer running
static volatile uint16_t sched_x;
static volatile uint16_t sched_y;
static volatile uint16_t sched_w;
static volatile uint16_t sched_h;



// --------------------------------------------------------
// accélérateur uc pendant effacement écran :
// buffer réservé pour effacement (on profite d'avoir plein de mémoire)
// les valeurs du dma pending sont stockées
// à la fin du dma d'effacement elles sont executées
// (mini scheduler)
// --------------------------------------------------------

#define FRAME_SIZE TFT_W * TFT_H * 2
__attribute__((aligned(32)))
static uint8_t tft_frame[FRAME_SIZE];    // 2bytes/pixel
//static 
//__attribute__((aligned(4)))
//uint8_t tft_frame_blk[FRAME_SIZE];    // 2bytes/pixel 
static uint32_t points[TFT_W];
static uint8_t refrCnt=0;

static void tft_init(void);

// ---------------------------------------------------------
// DMA IRQ : gestion dma_done
// ---------------------------------------------------------

void st_dma_wait(){                     // wait for end of current st buffer usage ; if blank en cours run
    while(true){
        uint32_t f = spin_lock_blocking(st_dma_lock); //protège dma_done_xxx contre un accès asynchrone

        // on continue si dma_done free (rien en cours) ou blank busy et sched free (blank en cours)
        // c-a-d si le buffer est dispo

        if(st_buffer_free) {
            st_buffer_free=false;
            spin_unlock(st_dma_lock, f);
            return;
        } 

        spin_unlock(st_dma_lock, f);            
        sleep_us(100);
        fillVoices();
    }
}

// ---------------------------------------------------------
// SPI helpers
// ---------------------------------------------------------
static inline void tft_cmd(uint8_t c) {
    gpio_put(ST7789_PIN_DC, 0);
    gpio_put(ST7789_PIN_CS, 0);
    spi_write_blocking(spi0, &c, 1);
    gpio_put(ST7789_PIN_CS, 1);
}

static inline void tft_data(const uint8_t *d, size_t len) {
    gpio_put(ST7789_PIN_DC, 1);
    gpio_put(ST7789_PIN_CS, 0);
    spi_write_blocking(spi0, d, len);
    gpio_put(ST7789_PIN_CS, 1);
}

static void tft_reset(void) {
    gpio_put(ST7789_PIN_RST, 0);
    delay_ms(20);
    gpio_put(ST7789_PIN_RST, 1);
    delay_ms(120);
}

// ---------------------------------------------------------
// WINDOW
// ---------------------------------------------------------
static void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t caset[] = { (uint8_t)(x0>>8), (uint8_t)(x0&0xFF), (uint8_t)(x1>>8), (uint8_t)(x1&0xFF) };
    uint8_t raset[] = { (uint8_t)(y0>>8), (uint8_t)(y0&0xFF), (uint8_t)(y1>>8), (uint8_t)(y1&0xFF) };

    tft_cmd(0x2A);
    tft_data(caset, 4);

    tft_cmd(0x2B);
    tft_data(raset, 4);

    tft_cmd(0x2C);
}

// -----------------------------------------------------------
// wait for end of current st dma usage 
// if blank running -> load sched and run
//
// x,y window upper left corner ; w,h size
// -----------------------------------------------------------
void st_dma_launch(uint8_t* frame,uint16_t x,uint16_t y,uint16_t w,uint16_t h){       

//uint8_t cnt=0;
//printf("launch cnt:%d stdf:%d stddb:%d stsf:%d\n",cnt,st_dma_free,st_dma_done_blank,st_sched_free);    
    while(1){ 
        
        uint32_t f = spin_lock_blocking(st_dma_lock); //protège st_dma_done_xxx et sched_xxx contre un accès asynchrone

        // si launch s'éxécute c'est que le buffer était dispo et 
        // st_dma_wait a mis st_buffer_free false pour bloquer d'autres demandes
        //
        // si st_dma_free run
        // sinon si st_dma_done_blank est false (blank en cours)
        // si sched dispo store and run
        // sinon wait    
        
        if(st_dma_free){            // dma free -> launch & run
            st_dma_free=false; 
            st_sched_free=true;
            
            tft_set_window(x, y, x + w - 1, y + h - 1);
            
            gpio_put(ST7789_PIN_DC, 1);
            gpio_put(ST7789_PIN_CS, 0);

            dma_channel_configure(
                st_dma_chan,
                &dma_cfg,
                &spi0_hw->dr,
                frame,
                w*h*2,
                true
            );
            spin_unlock(st_dma_lock, f);          
            return;
        }

        // donc dma busy 
        // if busy by blank & schedule dispo (réservée dans st_dma_wait) -> store & run
        // sinon wait
        else if (!st_dma_done_blank && st_sched_free){
            sched_frame=frame;
            sched_size=w*h*2;
            sched_x=x;
            sched_y=y;
            sched_w=w;
            sched_h=h;
            st_sched_free=false;
            spin_unlock(st_dma_lock, f);
            return;
        }

        spin_unlock(st_dma_lock, f);
        sleep_us(100);
    }
}

void st_dma_wait_blank(){       // wait for end of current st dma usage -- special pour blank : attente obligatoire
    while(true){
        uint32_t f = spin_lock_blocking(st_dma_lock); //protège dma_done contre un accès asynchrone
        if(st_dma_free) {
            st_dma_free=false;
            st_dma_done_blank=false;
            spin_unlock(st_dma_lock, f);
            return;}          
        spin_unlock(st_dma_lock, f);
    }
}

volatile bool get_st_dma_free(){return st_dma_free;}

void __not_in_flash_func(st_dma_irq_handler)() {
    while (spi_is_busy(spi0)) {
        tight_loop_contents();
    }

    if(!st_dma_done_blank){             // fin de blank
        st_dma_done_blank=true;
    }

    if(!st_sched_free){                 // sched en attente 
        
        tft_set_window(sched_x, sched_y, sched_x + sched_w - 1, sched_y + sched_h - 1);
        
        gpio_put(ST7789_PIN_DC, 1);
        gpio_put(ST7789_PIN_CS, 0);
        
        dma_channel_configure(          // launch pending
            st_dma_chan,
            &dma_cfg,
            &spi0_hw->dr,
            sched_frame,
            sched_size,
            true
        );
        st_sched_free=true;
    }
    else {                              // sched free ; nothing to launch
        gpio_put(ST7789_PIN_CS, 1);     // fin transfert 
        st_buffer_free=true;           
        st_dma_free=true;
    }
    dma_hw->ints0 = 1u << st_dma_chan;   // clear IRQ
}

// ---------------------------------------------------------
// DMA init
// ---------------------------------------------------------
int init_dma_spi() {
    st_dma_chan = dma_claim_unused_channel(true);
    if(st_dma_chan<0){return -1;}                   // no channel available

    // dma_cfg 
    dma_cfg = dma_channel_get_default_config(st_dma_chan);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&dma_cfg, true);
    channel_config_set_write_increment(&dma_cfg, false);
    channel_config_set_dreq(&dma_cfg, DREQ_SPI0_TX);
   
    // dma_cfg_single (blank only)
    dma_cfg_single = dma_channel_get_default_config(st_dma_chan);
    channel_config_set_transfer_data_size(&dma_cfg_single, DMA_SIZE_16);
    channel_config_set_read_increment(&dma_cfg_single, false);
    channel_config_set_write_increment(&dma_cfg_single, false);
    channel_config_set_dreq(&dma_cfg_single, DREQ_SPI0_TX);

    dma_channel_set_irq1_enabled(st_dma_chan, true);
#ifndef GLOBAL_DMA_IRQ_HANDLER 
    irq_set_exclusive_handler(DMA_IRQ_1, st_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);
#endif
    return st_dma_chan;
}

int st7789_setup(uint32_t spiSpeed)
{
    gpio_init(ST7789_PIN_BL);  gpio_set_dir(ST7789_PIN_BL, GPIO_OUT);
    gpio_put(ST7789_PIN_BL, 0);
    gpio_init(ST7789_PIN_DC);  gpio_set_dir(ST7789_PIN_DC, GPIO_OUT);
    gpio_init(ST7789_PIN_RST); gpio_set_dir(ST7789_PIN_RST, GPIO_OUT);
    gpio_init(ST7789_PIN_CS);  gpio_set_dir(ST7789_PIN_CS, GPIO_OUT);
    gpio_put(ST7789_PIN_CS, 1);

    spi_init(spi0, spiSpeed);
    gpio_set_function(ST7789_PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(ST7789_PIN_MOSI, GPIO_FUNC_SPI);

    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    tft_init();
    if(init_dma_spi()<0){printf("st7789_Setup: no spi dma channel available\n");return -1;}
 
    st_dma_free = true;
    st_dma_lock = spin_lock_init(DMA_LOCK);
    //st_dma_lock = spin_lock_instance(DMA_LOCK);
    st_dma_done_blank = true;
    st_sched_free = true;

    //memset(tft_frame_blk,0x00,FRAME_SIZE);
    //blank(tft_frame_blk,FRAME_SIZE); inutile fill rect blk modifié un seul octet

    tft_fill_rect_blank(0,0,TFT_H,TFT_W);
    gpio_put(ST7789_PIN_BL, 1);
    printf("st7789_Setup done\n");
    delay_ms(100);
    return st_dma_chan;
}


// ---------------------------------------------------------
// INIT SCREEN ST7789
// ---------------------------------------------------------
static void tft_init(void) {
    tft_reset();   

    tft_cmd(0x01); // SWRESET
    delay_ms(150);

    tft_cmd(0x11); // SLPOUT
    delay_ms(150);

    // Color mode : 16 bits
    tft_cmd(0x3A);
    uint8_t colmod = 0x55;
    tft_data(&colmod, 1);

    // Memory access control (orientation + BGR)
    tft_cmd(0x36);
    uint8_t madctl = 0x78;  // simple orientation
    tft_data(&madctl, 1);

    // Porch setting
    tft_cmd(0xB2);
    uint8_t b2[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    tft_data(b2, sizeof(b2));

    // Gate control
    tft_cmd(0xB7);
    uint8_t b7 = 0x35;
    tft_data(&b7, 1);

    // VCOM
    tft_cmd(0xBB);
    uint8_t bb = 0x19;
    tft_data(&bb, 1);

    // LCM control
    tft_cmd(0xC0);
    uint8_t c0 = 0x2C;
    tft_data(&c0, 1);

    // VDV VRH enable
    tft_cmd(0xC2);
    uint8_t c2 = 0x01;
    tft_data(&c2, 1);

    // VRH
    tft_cmd(0xC3);
    uint8_t c3 = 0x12;
    tft_data(&c3, 1);

    // VDV
    tft_cmd(0xC4);
    uint8_t c4 = 0x20;
    tft_data(&c4, 1);

    // Frame rate
    tft_cmd(0xC6);
    uint8_t c6 = 0x0F;
    tft_data(&c6, 1);

    // Power control
    tft_cmd(0xD0);
    uint8_t d0[] = {0xA4, 0xA1};
    tft_data(d0, sizeof(d0));

    // Positive gamma
    tft_cmd(0xE0);
    uint8_t e0[] = {
        0xD0,0x08,0x11,0x08,0x0C,0x15,0x39,0x33,
        0x50,0x36,0x13,0x14,0x29,0x2D
    };
    tft_data(e0, sizeof(e0));

    // Negative gamma
    tft_cmd(0xE1);
    uint8_t e1[] = {
        0xD0,0x08,0x10,0x08,0x06,0x06,0x39,0x44,
        0x51,0x0B,0x16,0x14,0x2F,0x31
    };
    tft_data(e1, sizeof(e1));

    tft_cmd(0x21); // INVON
    tft_cmd(0x29); // DISPON
    delay_ms(100);
}


// ---------------------------------------------------------
// FILL : 1 DMA = tout l'écran
// ---------------------------------------------------------
void tft_fill(uint16_t color) {

    //printf("done:%d blank:%d sched:%d",st_dma_free,st_dma_done_blank,st_sched_free);
    st_dma_wait();

    // remplir le buffer complet
    for (int i = 0; i < TFT_W * TFT_H; i++) {
        tft_frame[2*i]     = color >> 8;
        tft_frame[2*i + 1] = color & 0xFF;
    }

    st_dma_launch(tft_frame,0,0,TFT_W,TFT_H);    
}

// ---------------------------------------------------------
// FILL : 1 DMA = un rectangle dans l'écran
// ---------------------------------------------------------
void tft_fill_rect(uint16_t beg_line,uint16_t beg_col,uint16_t lines_nb,uint16_t col_nb,uint16_t color)
{

    st_dma_wait();

    // 1) buffer EXACT de la taille du pavé
    size_t total_pixels = lines_nb * col_nb;

    // 2) remplir le buffer
    for (int i = 0; i < total_pixels; i++) {
        tft_frame[2*i]     = color >> 8;
        tft_frame[2*i + 1] = color & 0xFF;
    }

    //tft_set_window(beg_col,beg_line,beg_col+col_nb-1,beg_line+lines_nb-1);

    st_dma_launch(tft_frame,beg_col,beg_line,col_nb,lines_nb);     

}

// ---------------------------------------------------------
// BLANK : 1 DMA = un rectangle dans l'écran
// ---------------------------------------------------------
void __not_in_flash_func(tft_fill_rect_blank)(uint16_t beg_line,uint16_t beg_col,uint16_t lines_nb,uint16_t col_nb)
{

    st_dma_wait_blank();

    size_t total_pixels = lines_nb * col_nb;
    size_t total_bytes  = total_pixels * 2;

    tft_set_window(beg_col,beg_line,beg_col+col_nb-1,beg_line+lines_nb-1);

    gpio_put(ST7789_PIN_DC, 1);
    gpio_put(ST7789_PIN_CS, 0);

    dma_channel_configure(
        st_dma_chan,
        &dma_cfg_single,
        &spi0_hw->dr,
        &zero16,          
        total_bytes,
        true
    );

}

// ---------------------------------------------------------
// DRAW : 1 DMA = un rectangle dans l'écran
// ---------------------------------------------------------
void __not_in_flash_func(tft_draw_rect)(uint16_t beg_line,uint16_t beg_col,uint16_t lines_nb,uint16_t col_nb,uint8_t* buffer)
{

    st_dma_wait();

    st_dma_launch(buffer,beg_col,beg_line,col_nb,lines_nb);      

}

// ---------------------------------------------------------
// CHARACTER 11x12 dma
// ---------------------------------------------------------
void tft_draw_char_11x12(uint16_t y, uint16_t x,
                         char c,
                         uint16_t color_fg,
                         uint16_t color_bg)
{
    
    st_dma_wait();
    
    const uint16_t *glyph = font11x12[(uint8_t)c];

    int w = 11;
    int h = 12;

    int idx = 0;

    for (int row = 0; row < h; row++) {
        uint16_t bits = glyph[row];

        for (int col = 0; col < w; col++) {
            uint16_t color =
                (bits & (1 << (w - 1 - col))) ? color_fg : color_bg;
                //(bits & (1 << col)) ? color_fg : color_bg;

            tft_frame[idx++] = color >> 8;
            tft_frame[idx++] = color & 0xFF;
        }
    }

    st_dma_launch(tft_frame,x,y,w,h);      

}

// ---------------------------------------------------------
// CHARACTER 12x12 dma
// ---------------------------------------------------------
void tft_draw_char_12x12(uint16_t y, uint16_t x,
                         char c,
                         uint16_t color_fg,
                         uint16_t color_bg)
{
    
    st_dma_wait();
    
    const uint16_t *glyph = font12x12[(uint8_t)c];

    int w = 12;
    int h = 12;

    int idx = 0;

    for (int row = 0; row < h; row++) {
        uint16_t bits = glyph[row];

        for (int col = 0; col < w; col++) {
            uint16_t color =
                (bits & (1 << (w - 1 - col))) ? color_fg : color_bg;
                //(bits & (1 << col)) ? color_fg : color_bg;

            tft_frame[idx++] = color >> 8;
            tft_frame[idx++] = color & 0xFF;
        }
    }

    st_dma_launch(tft_frame,x,y,w,h);      

}

// ---------------------------------------------------------
// TEXTE 11x12 dma
// ---------------------------------------------------------
void tft_draw_text_11x12_dma(uint16_t y, uint16_t x,
                         const char *s,
                         uint16_t color_fg,
                         uint16_t color_bg)
{
    st_dma_wait();
    
    while (*s) {
        tft_draw_char_11x12(y, x, *s, color_fg, color_bg);
        x += 11;   // avance de 12 pixels
        s++;
    }
}

// ---------------------------------------------------------
// TEXTE 12x12 dma
// ---------------------------------------------------------
void tft_draw_text_12x12_dma(uint16_t y, uint16_t x,
                         const char *s,
                         uint16_t color_fg,
                         uint16_t color_bg)
{
    st_dma_wait();
    
    while (*s) {
        tft_draw_char_12x12(y, x, *s, color_fg, color_bg);
        x += 12;   // avance de 12 pixels
        s++;
    }
}

// ---------------------------------------------------------
// TEXTE 12x12 pooling
// ---------------------------------------------------------
void tft_draw_text_12x12_block(
    uint16_t x,
    uint16_t y,
    const char *s,
    uint16_t fg,
    uint16_t bg)
{
    st_dma_wait();

    int len = 0;
    while (s[len]) len++;

    int w = len * 12;
    int h = 12;

    int idx = 0;

    for (int ligne = 0; ligne < 12; ligne++) {

        for (int car = 0; car < len; car++) {

            const uint16_t *glyph = font12x12[(uint8_t)s[car]];
            uint16_t bits = glyph[ligne];

            for (int bit = 0; bit < 12; bit++) {

                uint16_t color =
                    (bits & (1 << (11 - bit))) ? fg : bg;

                tft_frame[idx++] = color >> 8;
                tft_frame[idx++] = color & 0xFF;
            }
        }
    }

    st_dma_launch(tft_frame,x,y,w,h);         

}

// ---------------------------------------------------------
// TEXTE multiple de 11x12 dma
// ---------------------------------------------------------
void tft_draw_text_11x12_dma_mult(uint16_t x,uint16_t y,const char *s,uint16_t fg,uint16_t bg,int8_t mult)
{

    st_dma_wait();

    if(mult<1){mult=1;}

    int len = 0;
    while (s[len]) len++;if(len>31){printf("tft_draw_text_11x12_dma_mult ovf\n");while(1){sleep_ms(250);gpio_put(LED,0);sleep_ms(250);gpio_put(LED,1);}};

    uint8_t st=0;if(mult>1){st=2;}  // rétrécit la largeur/hauteur des caractères en mode mult
    int idx = 0;

    int w = len * (11-st);
    int h = 12-st;

    for (int ligne = 0; ligne < (12-st); ligne++) {

        for (int car =0; car < len; car++) {

            const uint16_t *glyph = font11x12[(uint8_t)s[car]];
            uint16_t bits = glyph[ligne];

            for (int bit = 0; bit < (11-st); bit++) {       // la fonte est 11x12 on utilise 10x10 en mult

                uint16_t color =
                    (bits & (1 << (10 - bit-st))) ? fg : bg;

                tft_frame[idx++] = color >> 8;
                tft_frame[idx++] = color & 0xFF;
                for(int8_t i=0;i<mult-1;i++){               // duplic horizontale
                    tft_frame[idx] = tft_frame[idx-2];
                    tft_frame[idx+1] = tft_frame[idx-1];
                    idx+=2;                    
                }
            }
        }
        uint16_t curr=idx;                                  // duplic verticale
        for(int8_t i=0;i<(mult-1);i++){
            memcpy(&tft_frame[curr+(len*(11-st)*2*mult)*i], &tft_frame[curr - len * (11-st) * 2 * mult], len * (11-st) * 2 * mult);
            idx+=len*(11-st)*2*mult;
        }
    }

    //printf("dt12dmam x:%d y:%d w:%d h:%d s:%s\n",x,y,w*mult,h*mult,s);
    st_dma_launch(tft_frame,x,y,w*mult,h*mult);    

}

// ---------------------------------------------------------
// TEXTE multiple de 12x12 dma
// ---------------------------------------------------------
void tft_draw_text_12x12_dma_mult(uint16_t x,uint16_t y,const char *s,uint16_t fg,uint16_t bg,int8_t mult)
{

    st_dma_wait();

    if(mult<1){mult=1;}

    int len = 0;
    while (s[len]) len++;if(len>31){printf("tft_draw_text_12x12_dma_mult ovf\n");while(1){sleep_ms(250);gpio_put(LED,0);sleep_ms(250);gpio_put(LED,1);}};

    uint8_t st=0;if(mult>1){st=2;}  // rétrécit la largeur/hauteur des caractères en mode mult
    int idx = 0;

    int w = len * (12-st);
    int h = 12-st;

    for (int ligne = 0; ligne < (12-st); ligne++) {

        for (int car =0; car < len; car++) {

            const uint16_t *glyph = font12x12[(uint8_t)s[car]];
            uint16_t bits = glyph[ligne];

            for (int bit = 0; bit < (12-st); bit++) {       // la fonte est 12x12 on utilise 10x10 en mult

                uint16_t color =
                    (bits & (1 << (11 - bit-st))) ? fg : bg;

                tft_frame[idx++] = color >> 8;
                tft_frame[idx++] = color & 0xFF;
                for(int8_t i=0;i<mult-1;i++){               // duplic horizontale
                    tft_frame[idx] = tft_frame[idx-2];
                    tft_frame[idx+1] = tft_frame[idx-1];
                    idx+=2;                    
                }
            }
        }
        uint16_t curr=idx;                                  // duplic verticale
        for(int8_t i=0;i<(mult-1);i++){
            memcpy(&tft_frame[curr+(len*(12-st)*2*mult)*i], &tft_frame[curr - len * (12-st) * 2 * mult], len * (12-st) * 2 * mult);
            idx+=len*(12-st)*2*mult;
        }
    }

    //printf("dt12dmam x:%d y:%d w:%d h:%d s:%s\n",x,y,w*mult,h*mult,s);
    st_dma_launch(tft_frame,x,y,w*mult,h*mult);    

}

uint16_t tft_draw_int_12x12_dma_mult(uint16_t x,uint16_t y,uint16_t fg,uint16_t bg,int8_t mult,int32_t num,uint8_t len){
    #define MAXL 32
    char st[MAXL];
    memset(st,0x00,MAXL);
    uint8_t l=convIntToString(st,num);
    if(len>l && len!=0){memset(st+l,' ',len-l);}
    tft_draw_text_12x12_dma_mult(x,y,st,fg,bg,mult);
    return strstr(st,"\0")-st;
}

uint16_t tft_draw_int_12x12_dma_mult(uint16_t x,uint16_t y,uint16_t fg,uint16_t bg,int8_t mult,int32_t num){
    return tft_draw_int_12x12_dma_mult(x,y,fg,bg,mult,num,0);
}

uint16_t tft_draw_float_12x12_dma_mult(uint16_t x,uint16_t y,uint16_t fg,uint16_t bg,int8_t mult,float num,uint8_t len){
    #define MAXL 32
    char st[MAXL];
    memset(st,0x00,MAXL);
    uint8_t l=convNumToString(st,num);
    if(len>l && len!=0){memset(st+l,' ',len-l);}
    tft_draw_text_12x12_dma_mult(x,y,st,fg,bg,mult);
    return strstr(st,"\0")-st;
}

uint16_t tft_draw_float_12x12_dma_mult(uint16_t x,uint16_t y,uint16_t fg,uint16_t bg,int8_t mult,float num){
    return tft_draw_float_12x12_dma_mult(x,y,fg,bg,mult,num,0);
}
// display scope lookout of buf values ; len =buf size ; f freq ; begline first available line ; 
// fd freq display ; wf required waveform ; mode calcul (0=i2s true data ; 1=computed with ce & cr ; 2 =lfo_mode one waveform output)
// begine first line after title ; zline 0 line position =0 middle, =1 bottom
void __not_in_flash_func(scope)(int32_t* buf,float f,uint16_t begline,bool fd,bool blk,uint8_t refr,uint8_t wf,uint8_t mode_calcul,uint8_t object,int16_t zline){
        
    if(refrCnt>=refr){
        
        refrCnt=0;   
        uint32_t x;
        uint16_t bgcolor=0x0000;
        uint16_t fgcolor=0x07ef;
        int32_t yy;
        uint8_t zz=2;if(zline!=0){zz=zline;}                        
        uint32_t v;

        st_dma_wait();

        uint8_t* tftf=(uint8_t*)tft_frame;
        uint32_t len=(TFT_H-begline)*TFT_W*2;

        /*tftf--;
        do {
            *tftf++ = bgcolor;
            len--;
        } while (len > 0);*/
        blank(tftf,len);

        int8_t sign=1;
        float b;
        int32_t rc;
        const int16_t *rcTableCurr;
        const int16_t *rcTable32;

        if(mode_calcul==1){                                     // one time init
            rc=buf[0]>>16;
            if(rc>32){rc=64-rc;}
            rcTableCurr = &rc_tables[rc][0][0];
            rcTable32 = &rc_tables[32][0][0]; 
        }

        for(uint32_t i=0;i<TFT_W;i++){                          // read buf and generate waveform (if comput mode = 0 : 1 column for 1 sample)
            
            switch(mode_calcul){                                // b value computation (y=(float) 0-1)
                
                case 0:{                                        // i2s true data
                    int32_t t=buf[i*2];            
                    b=(float)t/(float)0x7fffffff;
                    }break;

                case 1:{                                        // computed view with ce&cr
                    uint32_t ce=buf[i];                         // ce : 16 bits gauche = rc, 16 bits droite num ech
                    uint32_t rc=ce>>16;  
            
                    ce &= (BASIC_WAVE_TABLE_LEN-1);             // local currEch (cyclic ratio managment)

                    bool vv=(ce<RC_TABLES_LEN);
                    sign=(vv*2-1);                              // invert 180-360°
    
                    ce ^= (!vv) * (BASIC_WAVE_TABLE_LEN - 1);   // ce = vv*ce+!vv*((BASIC_WAVE_TABLE_LEN-1) - ce);  // invert 180-360°           
                    ce &= RC_TABLES_LEN-1;

                    uint32_t ce_idx = ce;
                    if (rc > 32){ce_idx = (RC_TABLES_LEN - 1) - ce_idx;}

                    const int16_t* w=rcTableCurr+RC_N_WAVES*ce_idx; // rc_table values ptr
            
                    if(wf==WSIN || wf==WTRI){b=(float)w[wf]/(float)0x7fff;}       // sample full scale ratio
                    else if(wf==WSAW){
                        int16_t tri=rcTable32[RC_N_WAVES*ce+WTRI];      // saw utilise la table 32 du triangle
                        int32_t saw;
                        if(ce<(RC_N_SAMPLES >> 1)){saw=(65536-tri)>>1;}
                        else saw=tri>>1;
                        if(rc>=32){saw=-saw;}
                        b=(float)saw/(float)0x7fff;
                    }
                    else if(wf==WSQR){
                        int16_t sqr=(ce & (BASIC_WAVE_TABLE_LEN>>1)) ? -0x7fff : 0x7fff;
                        b=(float)sqr/(float)0x7fff;
                    }
                    }break;

                case 2:{                                                // lfo real wave view 
                    int32_t t=buf[object*OSC_SCOPE_BUFFER_LEN*BASIC_WAVES_NB + i*BASIC_WAVES_NB+wf];     
                    b=(float)t/(float)0x7fff;     
                    }break;

                case 3:{                                                // adsr real wave view (ampl 16bits)
                    uint32_t t=buf[object*ADSR_SCOPE_BUFFER_LEN+i];     
                    b=(float)t/(float)0x0000ffff;     
                    }break;

                default: break;
            }

            yy=(int32_t)(sign*b*((TFT_H-begline)/zz));                  // tft y value (tft x value = i)

//printf("obj:%d i:%d wf:%d b:%f yy:%i\n",object,i,wf,b,yy);

            if(abs(yy)>(TFT_H-begline)/zz){yy=sign*(TFT_H-begline)/zz;}
            v=2*(((TFT_H-begline-1)/zz-yy)*TFT_W+i);                      // pixel location in tft_frame
            tft_frame[v]=fgcolor;
            //printf("%u %u %i %f\n",object,i,yy,v);
        }

        for(uint8_t i=0;i<TFT_W;i+=3){tft_frame[2*(((TFT_H-begline-1)/zz)*TFT_W+i)]=fgcolor;}        // 0 line

        st_dma_launch(tft_frame,0,begline,TFT_W,TFT_H-begline);

        if(fd){tft_draw_float_12x12_dma_mult(TFT_W*1/3,0,0xf81f,0,1,f,6);}          // title line
    }
    else refrCnt++;
}

void __not_in_flash_func(scope)(int32_t* buf,float f,uint16_t begline,bool fd,bool blk,uint8_t refr,uint8_t wf,uint8_t mode_calcul,uint8_t object){
    scope(buf,f,begline,fd,blk,refr,wf,mode_calcul,0,0);
}

void __not_in_flash_func(scope)(int32_t* buf,float f,uint16_t begline,bool fd,bool blk,uint8_t refr,uint8_t wf,uint8_t mode_calcul){
    scope(buf,f,begline,fd,blk,refr,wf,mode_calcul,0);
}    

bool debug_ticker(){
    if(__builtin_expect((millisCounter-ticker10)>10000,0)){
        printf("10s_cnt:%d\n",millisCounter/10000);ticker10=millisCounter;
        tft_draw_int_12x12_dma_mult(TFT_W-4*12,0,0x001f,0x0000,1,millisCounter/10000); 
        return true;   
    }
    return false;
}


// -----------------------------------------------
//  test : text et fillings
// -----------------------------------------------

static uint8_t testCnt=0;
static uint32_t millis=0;
uint32_t ms=0,ms0=0;

uint16_t beg=0,l=beg,lbeg=0,m=0;

void init_test_7789(uint32_t mss,uint16_t l0,uint16_t c0,uint16_t ln,uint16_t cn,uint8_t m0){
    beg=l0;lbeg=l0,l=l0;m=m0;ms=mss;ms0=mss;
}

void test_st7789(){

    if((millis+ms)<millisCounter){
      millis=millisCounter;

      switch (testCnt++){

        case 0:
            tft_fill_rect(beg,0,TFT_W-beg,TFT_H, 0x0000);   //tft_fill(0x0000); // écran noir
            break;
        case 1:
            l+=12;
            tft_draw_text_12x12_block(0, l, "+HELLO World!", 0xFFFF, 0x0000);
            break;
        case 2:
            l+=13;
            tft_draw_text_12x12_block(0, l, "AaBbCcDdEeFfGgHhIiJj", 0xFFFF, 0x0000);
            break;
        case 3:
            l+=13;    
        tft_draw_text_12x12_block(0, l, "KkLlMmNnOoPpQqRrSsTt", 0xFFFF, 0x0000);
            break;
        case 4:
            l+=13;
            tft_draw_text_12x12_block(0, l, "UuVvWwXxYyZz", 0xFFFF, 0x0000);
            break;
        case 5:
            l+=13;
            tft_draw_text_12x12_block(0, l, "$+-*,/;:?\%&#()[]{}", 0xFFFF, 0x0000);
            break;
        case 6:
            l+=12*2;            
            tft_draw_text_12x12_dma_mult(0, l, "$+-*,/;:?", 0xFFFF, 0x0000,2);
            break;
        case 7:
            l+=12*2+2;
            tft_draw_text_12x12_dma_mult(0, l, "\%&#()[]{}", 0xFFFF, 0x0000,2);
            break;
        case 8:
            l+=12*2+2;
            tft_draw_text_12x12_dma_mult(0, l, "0123456789", 0xFFFF, 0x0000,2);
            break;
        case 9:
            l+=12*3-6;
            tft_draw_text_12x12_dma_mult(0, l, "01234567", 0xFFFF, 0x0000,3);
            break;
        case 10:
            tft_fill_rect(beg,0,TFT_W-beg,TFT_H, 0x0000);
            break;
        case 11:
            tft_fill_rect(50, 50, 140, 140, 0xFFFF);
            break;
        case 12:
            tft_fill_rect(50+beg-4,(TFT_W-(6*10*m))/2,4,(6*10*m), 0x0000);
            break;
        case 13:
            tft_draw_text_12x12_dma_mult((TFT_W-(6*10*m))/2,50+beg,"ST7789", 0xFFFF, 0x0000,m);
            break;        
        case 14:
            tft_fill_rect(beg,0,TFT_W-beg,TFT_H, 0xFFFF);
            tft_fill_rect(50, 50, 140, 140, 0x0000);
            tft_fill_rect(50+beg-4,(TFT_W-(6*10*m))/2,4,(6*10*m), 0xffff);
            tft_draw_text_12x12_dma_mult((TFT_W-(6*10*m))/2,50+beg, "ST7789", 0x0000, 0xFFFF,m);
            break;
        case 15:
            tft_fill_rect(beg,0,TFT_W-beg,TFT_H, 0x07E0); // vert
            tft_fill_rect(50, 50, 140, 140, 0x001F);
            break;
        case 16:
            tft_fill_rect(beg,0,TFT_W-beg,TFT_H, 0x001F); // rouge
            tft_fill_rect(50, 50, 140, 140, 0xF800);
            break;
        case 17:
            tft_fill_rect(beg,0,TFT_W-beg,TFT_H, 0xF800); // bleu
            tft_fill_rect(50, 50, 140, 140, 0x07E0);
            break;
        default:
            testCnt=0;
            l=beg;
            break;
      }
    }   
}

void __not_in_flash_func(test_st7789_2)(void)
{
    if((millis+ms0)<millisCounter){     
        millis=millisCounter;

      if(l>lbeg && ms0>2){                          // effacement ligne précédente 
        ms0=2;
        tft_fill_rect_blank(l-1,0,1,TFT_W);
        return;  
      }
      
      if(l>=TFT_H){l=lbeg;}

      ms0=ms;
      uint32_t bgad=l*TFT_W*2;                      // trace ligne courante
      memset(&tft_frame[bgad],0xff,TFT_W*2);
      tft_draw_rect(l,0,1,TFT_W,&tft_frame[bgad]);          
      l++;     
    }
    // ton code ici
}

/*void __not_in_flash_func(test_st7789_2)(void)
{
    if((millis+ms0)<millisCounter){     
        millis=millisCounter;

      if(l>lbeg && ms0>2){                          // effacement ligne précédente 
        ms0=2;
        tft_fill_rect_blank(l-1,0,1,TFT_W);
        return;  
      }
      
      if(l>=TFT_H){l=lbeg;}

      ms0=ms;
      uint32_t bgad=l*TFT_W*2;                      // trace ligne courante
      memset(&tft_frame[bgad],0xff,TFT_W*2);
      tft_draw_rect(l,0,1,TFT_W,&tft_frame[bgad]);          
      l++;     
    }
}*/