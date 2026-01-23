/* leds.cpp */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "ws2812.pio.h"
#include "const.h"
#include "st7789.h"

//static PIO pio = ws2812_pio;   // pio0 used by i2s
//static uint sm;
//static uint offset;

extern volatile uint32_t millisCounter;

static int ws_dma_chan;
static dma_channel_config dma_cfg;
static volatile bool ws_dma_done = false;

static PIO ws_pio;
static int ws_sm;

extern uint32_t gdis;

volatile bool get_ws_dma_done(){return ws_dma_done;}

void ws_dma_irq_handler() {
    dma_hw->ints0 = 1u << ws_dma_chan;   // clear IRQ
    ws_dma_done = true;
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
    sm_config_set_out_shift(&c, false, true, 24);                   // controle du shift register alimenté par le TX FIFO (,right,autopull,threshpld)
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);                  // concatène FIFO TX et RX (8 bytes)
    float div = 14;
    sm_config_set_clkdiv(&c, div);                                  // env 150/14 = 107nS
    pio_sm_init(ws_pio, ws_sm, offset, &c);                               // attache le programme et la structure à la sm                 
    
    pio_sm_clear_fifos(ws_pio, ws_sm);
    pio_sm_set_enabled(ws_pio, ws_sm, true);

    // dma init
    if(init_dma_ws()<0){printf("ledsWs2812Setup: no dma channel available\n");return -1;}
    
    ws_dma_done=true;

    printf("début ws2812\n");
    return ws_dma_chan;
}

void pio_sm_put_blocking_array(PIO pio, uint sm, const uint32_t *src, size_t len) {
    for (size_t i = 0; i < len; i++) {
        pio_sm_put_blocking(pio, sm, src[i]);
    }
}

void pio_sm_put_dma_array(PIO pio, uint sm, const uint32_t *src, size_t len) {
    //print_diag('1',gdis);
    while (!ws_dma_done) {tight_loop_contents();}
    ws_dma_done=false;
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
#define NBLEDS 70
#define SPEC 16
uint32_t buf[NBLEDS];
#define COLNB 24
#define MINCOL 2
#define COLCYCLENB 2
uint32_t colPal[]={0x00000000,0xffffff00,0xff000000,0x00ff0000,0x0000ff00,0xffff0000,0x00ffff00,
        0xff00ff00,0xff800000,0x8000ff00,0xff40a000,0x840ffe00,0x40a0ff00,0x40ff4000,0xffd00000,
        0xc0c0c000,0x80808000,0x80000000,0x80800000,0x00008000,0x00808000,0x4000a000,0xff007000,0x00a0ff00};
static uint32_t millis=0;

void ws_show_3(uint32_t ms){

    if((millis+ms)<millisCounter){

        //gpio_put(TEST_PIN,HIGH);    // 170uS @70leds + calculs

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

            if(pt>=0 && pt<SPEC){
                if(pt<SPEC/4 || pt>(SPEC-4)){buf[pos]=reduc(colPal[curCol],6);}
                else if(pt<(SPEC/2-1) || pt>(SPEC/2)){buf[pos]=reduc(colPal[curCol],5);}
                else if(pt!=(SPEC/2)){buf[pos]=reduc(colPal[curCol],4);}
                else {buf[pos]=reduc(colPal[curCol],3);}
            }
            else {buf[pos]=reduc(colPal[curCol],7);}
        }

        pio_sm_put_dma_array(ws_pio,ws_sm,buf,NBLEDS);

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

