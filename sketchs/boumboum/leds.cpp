/* leds.cpp */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/sync.h"

#include "ws2812.pio.h"
#include "const.h"
//#include "st7789.h"

extern volatile uint32_t millisCounter;

static int ws_dma_chan;
static dma_channel_config dma_cfg;
static volatile bool ws_dma_done = false;
static volatile uint32_t ws_dma_done_us = 0;   // time of the end of the last ws DMA transfer
static spin_lock_t *ws_dma_lock;

static PIO ws_pio;
static int ws_sm;

extern uint32_t gdis;


uint32_t colPal[COLNB]={       // format 0xGGRRBB00, values for LEDs (light proportional to value)
    0x00000000,      //  0    WS_COL_OFF
    0xffffff00,      //  1    WS_COL_WHITE
    0x00ff0000,      //  2    WS_COL_RED
    0x17ff0000,      //  3    WS_COL_ORANGE_RED
    0x3fff0000,      //  4    WS_COL_ORANGE
    0x85ff0000,      //  5    WS_COL_AMBER
    0xffff0000,      //  6    WS_COL_YELLOW
    0xff670000,      //  7    WS_COL_LIME
    0xff000000,      //  8    WS_COL_GREEN
    0xff003700,      //  9    WS_COL_SPRING_GREEN
    0xff00a900,      // 10    WS_COL_AQUAMARINE
    0xff00ff00,      // 11    WS_COL_CYAN
    0x6700ff00,      // 12    WS_COL_SKY_BLUE
    0x1700ff00,      // 13    WS_COL_AZURE
    0x0000ff00,      // 14    WS_COL_BLUE
    0x002fff00,      // 15    WS_COL_VIOLET
    0x0085ff00,      // 16    WS_COL_PURPLE
    0x00ffff00,      // 17    WS_COL_MAGENTA
    0x00ff6700,      // 18    WS_COL_FUCHSIA
    0x00ff2500,      // 19    WS_COL_DEEP_PINK
    0x43ff4300,      // 20    WS_COL_SALMON
    0x80ff4300,      // 21    WS_COL_PEACH
    0xeaff4300,      // 22    WS_COL_PALE_YELLOW
    0xff8f4300,      // 23    WS_COL_PALE_LIME
    0xff437200,      // 24    WS_COL_MINT
    0xea43ff00,      // 25    WS_COL_PALE_CYAN
    0x8043ff00,      // 26    WS_COL_PALE_SKY
    0x4343ff00,      // 27    WS_COL_PALE_BLUE
    0x438fff00,      // 28    WS_COL_LAVENDER
    0x43ffea00,      // 29    WS_COL_PALE_MAGENTA
    0x43ff8000,      // 30    WS_COL_PINK
    0xc0ff8300,      // 31    WS_COL_WARM_WHITE
};

volatile bool get_ws_dma_done(){return ws_dma_done;}

void ws_dma_wait(){                     // wait for the end of the current ws DMA transfer, then set ws_dma_done to false ».
    uint32_t save;                      
    while(true){
        uint32_t f = spin_lock_blocking(ws_dma_lock); //save = save_and_disable_interrupts();
        if(ws_dma_done) {
            ws_dma_done=false;
            spin_unlock(ws_dma_lock, f);                  // restore_interrupts(save);
            return;}
        spin_unlock(ws_dma_lock, f);                  // restore_interrupts(save);
        sleep_us(100);
    }
}

void __not_in_flash_func(ws_dma_irq_handler)() {
    ws_dma_done = true;
    ws_dma_done_us = time_us_32();
    dma_hw->ints1 = 1u << ws_dma_chan;   // clear IRQ
}

int init_dma_ws() {
    ws_dma_chan = dma_claim_unused_channel(true);
    if(ws_dma_chan<0){return -1;}                   // no channel available
    //printf("dma_chan = %d\n", dma_chan);
    dma_cfg = dma_channel_get_default_config(ws_dma_chan);

    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, true);
    channel_config_set_write_increment(&dma_cfg, false);
    channel_config_set_dreq(&dma_cfg,WS2812_DREQ_PIO_TX0);                   // voir commentaire dans main

    dma_channel_set_irq1_enabled(ws_dma_chan, true);
#ifndef GLOBAL_DMA_IRQ_HANDLER
    irq_set_exclusive_handler(DMA_IRQ_1, global_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);
#endif
    return ws_dma_chan;
}

int ledsWs2812Setup(PIO pio,uint8_t ledPin) {

    ws_pio=pio;

    // get sm
    ws_sm = pio_claim_unused_sm(pio, true);
    if(ws_sm<0){printf("ledsWs2812Setup: no sm available\n");return -2;}

    printf("ledsWs2812Setup pio:%d sm:%d\n",pio_get_index(pio),ws_sm);
    uint offset = pio_add_program(ws_pio, &ws2812_program);

    // sm config
    pio_gpio_init(ws_pio, ledPin);                                     // attache le gpio au pio (si plusieurs gpio plusieurs inits)
    pio_sm_set_consecutive_pindirs(ws_pio, ws_sm, ledPin, 1, true);    // 1er,nbre,direction des gpio de la sm (correspond pour le pilotage sm à "gpio_set_dir()" en pilotage processeur)

    pio_sm_config c = ws2812_program_get_default_config(offset);    // créé la structure de la config de la sm
    sm_config_set_sideset_pins(&c, ledPin);                         // gpio de base de la sm qui sera associée à la structure
    sm_config_set_out_pins(&c, ledPin, 1);                          // direction des GPIOs de la sm (pas compris pourquoi il y a 2 couches de direction avec pio_sm_consecutive_pindirs)
    sm_config_set_out_shift(&c, false, true, 24);                   // controle du shift register alimenté par le TX FIFO (,right,autopull,threshold)
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);                  // concatène FIFO TX et RX (8 bytes)
    float div = 14;
    sm_config_set_clkdiv(&c, div);                                  // env 150/14 = 107nS
    pio_sm_init(ws_pio, ws_sm, offset, &c);                               // attache le programme et la structure à la sm

    pio_sm_clear_fifos(ws_pio, ws_sm);
    pio_sm_set_enabled(ws_pio, ws_sm, true);

    // dma init
    if(init_dma_ws()<0){printf("ledsWs2812Setup: no dma channel available\n");return -1;}

    ws_dma_done=true;
    ws_dma_lock = spin_lock_init(WS_LOCK);

    printf("début ws2812\n");
    return ws_dma_chan;
}

void pio_sm_put_blocking_array(PIO pio, uint sm, const uint32_t *src, size_t len) {
    for (size_t i = 0; i < len; i++) {
        pio_sm_put_blocking(pio, sm, src[i]);
    }
}

void pio_sm_put_dma_array(PIO pio, uint sm, const uint32_t *src, size_t len) {

    ws_dma_wait();
    dma_channel_configure(ws_dma_chan,&dma_cfg,&pio->txf[sm],src,len,true);
}

// ___________ seq test _____________

void ledblink(uint16_t t)
{
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    sleep_ms(t);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
}

void perso(PIO pio, int sm){
printf("test perso\n");

    uint32_t ms=700;
    uint8_t lbvrj=4;
    uint32_t bvrj[] = {0xF8000000,0x0000F000,0x00F80000,0x00F0F000,0xF8000000,0x0000F000,0x00F80000,0x00F0F000};
    uint8_t cnt=0;

    cnt=0;
    while(cnt<4){
        pio_sm_put_dma_array(pio,sm,bvrj+cnt,lbvrj);sleep_ms(ms);cnt++;
    }
}

// leds[] byte : bits 0-4 = colour (0 to 31, 0 = off) ; bits 5-7 = brightness (0 = dimmest visible, 7 = full)

static uint16_t dimCoef[8] = DIM_COEFS;         // brightness factors /256, in RAM for speed

// Returns 0x01 in each byte of x that is non-zero, 0x00 elsewhere
static inline uint32_t nonZeroBytes(uint32_t x)
{
    return ((((x & 0x7F7F7F7F) + 0x7F7F7F7F) | x) >> 7) & 0x01010101;
}

// Scale a color by k/256 (k = 0..256), rounded to nearest ; non-zero components stay at least 1
// bytes 0 and 2 are scaled by one multiplication, bytes 1 and 3 by another
static inline uint32_t dimColorK(uint32_t color, uint32_t k)
{
    uint32_t even = ((((color & 0x00FF00FF) * k) + 0x00800080) >> 8) & 0x00FF00FF;
    uint32_t odd  = ((((color >> 8) & 0x00FF00FF) * k) + 0x00800080) & 0xFF00FF00;
    uint32_t d = even | odd;
    return d | (nonZeroBytes(color) & ~nonZeroBytes(d));    // keep non-zero components visible
}

// Scale a color to brightness level 0 (dimmest visible) .. 7 (full)
uint32_t __not_in_flash_func(dimColorFast)(uint32_t color, uint8_t level)
{
    return dimColorK(color, dimCoef[level & 7]);
}

static_assert(NB_CORES_USED == 1,
    "bbLeds/bbLedsDirty rely on the write order between the main loop and an interrupt on the same core: "
    "with 2 cores, protect them with a spinlock shared by setWsLeds and bb_leds_handler");

/*  chaque objet a ses leds décrites par (position, longueur) dans la chaîne des ws
    lorsqu'un objet veut afficher ses leds, il passe (pointeur,position,longueur,modifs)
    à setWsLeds qui encode la modif dans bbLeds et lève bbLedsDirty ;
    bb_leds_handler (irq 1 kHz) lance le transfert DMA toutes les WS_REFRESH_MS ms si bbLeds a changé
*/

static uint32_t bbLeds[NBLEDS];                 // read by DMA: must not be on the stack
static volatile bool bbLedsDirty = false;       // true when bbLeds has changed since the last transfer

// Called from the main loop only: encodes the modified LEDs into bbLeds, never waits
bool __not_in_flash_func(setWsLeds)(const uint8_t* leds, uint8_t pos, uint8_t len, const bool* modifs)
{
    if((pos+len > NBLEDS) || (len==0)){return false;}

    for(uint8_t i=0;i<len;i++){
        if(modifs[i]){
            uint8_t b=leds[i];
            bbLeds[i+pos] = dimColorK(colPal[b & 0x1f], dimCoef[(b >> 5) & 7]);   // colPal[0] is black: 0 at any level
        }
    }
    bbLedsDirty = true;                         // set AFTER the writes (see bb_leds_handler)
    return true;
}

// Called by the 1 kHz interrupt: every WS_REFRESH_MS ms, sends bbLeds if it has changed, never waits
// bbLedsDirty is cleared BEFORE starting the transfer: if this interrupt occurs while setWsLeds
// is writing, the remaining writes set the flag again and are sent at the next period
void __not_in_flash_func(bb_leds_handler)()
{
    static uint8_t tick = 0;

    if(++tick < WS_REFRESH_MS){return;}
    tick = 0;

    if(!bbLedsDirty || !ws_dma_done || (time_us_32() - ws_dma_done_us) < WS_LATCH_US){return;}

    bbLedsDirty = false;
    ws_dma_done = false;
    dma_channel_configure(ws_dma_chan,&dma_cfg,&ws_pio->txf[ws_sm],bbLeds,NBLEDS,true);
}

uint32_t reduc(uint32_t col,uint8_t reduc){
    uint32_t col1=((col &0x0000ff00)>> reduc) &0x0000ff00 ;      // b
    uint32_t col2=((col &0x00ff0000)>> reduc) &0x00ff0000 ;      // r
    uint32_t col3=((col &0xff000000)>> reduc) &0xff000000 ;      // v

    return (col3 | col2 | col1);
}

static uint16_t cyclePos=0;
static uint8_t colCycle=0;
static uint8_t col=2;
static uint8_t newCol=2;
uint32_t bufLeds[NBLEDS];

static uint32_t millis=0;

void ws_show_3(uint32_t ms){

    if((millis+ms)<millisCounter){
        //gpio_put(TEST_PIN,HIGH);    // 970uS @70leds + calculs

        millis=millisCounter;

        int16_t pt;
        uint16_t pos,curCol;

        for(uint16_t p=0;p<NBLEDS;p++){
            pos=cyclePos+p;if(pos>=NBLEDS){pos-=NBLEDS;}

            curCol=col;
            if((pos<cyclePos)){curCol=newCol;}

            pt=-1;
            if((pos<cyclePos) && (pos>=(cyclePos-SPEC)) && (cyclePos>=SPEC)){pt=pos-(cyclePos-SPEC);}
            else if((pos<cyclePos) && (cyclePos<SPEC)){pt=SPEC-(cyclePos-pos);}
            else if((pos>(NBLEDS-SPEC+cyclePos)) && (cyclePos<SPEC)){pt=pos-(NBLEDS-SPEC+cyclePos);}

            uint32_t k = WS3_K_BACK;                                            // outside the light spot
            if(pt>=0 && pt<SPEC){
                if(pt<SPEC/4 || pt>(SPEC-4)){k = WS3_K_EDGE;}
                else if(pt<(SPEC/2-1) || pt>(SPEC/2)){k = WS3_K_MID;}
                else if(pt!=(SPEC/2)){k = WS3_K_NEAR;}
                else {k = WS3_K_CENTER;}
            }
            bufLeds[pos]=dimColorK(colPal[curCol],k);
        }

        pio_sm_put_dma_array(ws_pio,ws_sm,bufLeds,NBLEDS);

        cyclePos++;if(cyclePos>=NBLEDS){
            cyclePos=0;
            colCycle++;
            if(colCycle>=COLCYCLENB){
                colCycle=0;
                newCol=col+1;if(newCol>=COLNB){newCol=MINCOL;}
                //char color[]={(char)(48+newCol/10),(48+newCol-newCol/10*10),0x00};
                //tft_draw_text_12x12_dma(20, 100,color, 0xFFFF, 0x0000,3);
            }
            else col=newCol;
        }

        //gpio_put(TEST_PIN,LOW);
    }
}