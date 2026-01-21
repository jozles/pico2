/* testWs2812 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "ws2812.pio.h"

#define PIN_WS2812 0   // GPIO0

#define NB_LEDS 70

static PIO ws_pio;
static int ws_sm;
static int ws_dma_chan;
static dma_channel_config dma_cfg;
static volatile bool dma_done = false;

void ledblink(uint16_t t)
{
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    sleep_ms(t);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
}

uint8_t cnt_irq=0;
void ws_dma_irq_handler() {
    cnt_irq++;
    if(cnt_irq>2){
        dma_hw->ints0 = 1u << ws_dma_chan;   // clear IRQ
        dma_done = true;
    }
}

void init_dma_ws() {
    ws_dma_chan = dma_claim_unused_channel(true);
    //printf("dma_chan = %d\n", dma_chan);
    dma_cfg = dma_channel_get_default_config(ws_dma_chan);

    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, true);
    channel_config_set_write_increment(&dma_cfg, false);
    channel_config_set_dreq(&dma_cfg, DREQ_PIO0_TX0);                   // voir commentaire dans main

    dma_channel_set_irq1_enabled(ws_dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_1, ws_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);
}

void ws_dma_setup(PIO pio,int sm){
    
    ws_pio=pio;
    ws_sm=sm;

    uint offset = pio_add_program(pio, &ws2812_program);

    // Configurer le state machine
    pio_gpio_init(ws_pio, PIN_WS2812);                              // attache le gpio au pio (si plusieurs gpio plusieurs inits)
    pio_sm_set_consecutive_pindirs(ws_pio, ws_sm, PIN_WS2812, 1, true);   // 1er,nbre,direction des gpio de la sm (correspond pour le pilotage sm à "gpio_set_dir()" en pilotage processeur)

    pio_sm_config c = ws2812_program_get_default_config(offset);    // créé la structure de la config de la sm
    sm_config_set_sideset_pins(&c, PIN_WS2812);                     // gpio de base de la sm qui sera associée à la structure                 
    sm_config_set_out_pins(&c, PIN_WS2812, 1);                      // direction des GPIOs de la sm (pas compris pourquoi il y a 2 couches de direction avec pio_sm_consecutive_pindirs)                 
    sm_config_set_out_shift(&c, false, true, 24);                   // controle du shift register alimenté par le TX FIFO (,right,autopull,threshpld)
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);                  // concatène FIFO TX et RX (8 bytes)
    float div = 14;
    sm_config_set_clkdiv(&c, div);                                  // 150/14 ~ 107nS
    pio_sm_init(ws_pio, ws_sm, offset, &c);                         // attache le programme et la structure à la sm 
        
    pio_sm_clear_fifos(ws_pio, ws_sm);
    pio_sm_set_enabled(ws_pio, ws_sm, true);

    init_dma_ws();

    dma_done=true;
}

static inline void ws2812_put_pixel(PIO pio, int sm, uint32_t color) {
    // WS2812 attend GRB sur 24 bits
    uint32_t grb = ((color << 8) & 0xFF0000) | ((color >> 8) & 0x00FF00) | (color & 0x0000FF);
    pio_sm_put_blocking(pio, sm, grb << 8); // <<8 pour n’envoyer que 24 bits
}

void pio_sm_put_blocking_array(PIO pio, uint sm, const uint32_t *src, size_t len) {
    for (size_t i = 0; i < len; i++) {
        pio_sm_put_blocking(pio, sm, src[i]);
    }
}

void pio_sm_put_dma_array(PIO pio, uint sm, const uint32_t *src, size_t len) {
    while (!dma_done) {tight_loop_contents();}
    dma_done=false;
    dma_channel_configure(ws_dma_chan,&dma_cfg,&pio->txf[sm],src,len,true);   
}

void perso(PIO pio, int sm){
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

void perso2(PIO pio, int sm){

    uint8_t spec=16;
    uint16_t coloopmax=4;
    uint16_t nbLeds=70;
    uint8_t nbcol=24;
    uint8_t mincol=2;
    uint8_t newcol=mincol;
    uint8_t oldcol=mincol;
    uint8_t ccur=mincol;
    uint32_t colbuf[]={0x00000000,0xffffff00,0xff000000,0x00ff0000,0x0000ff00,0xffff0000,0x00ffff00,
            0xff00ff00,0xff800000,0x8000ff00,0xff40a000,0x840ffe00,0x40a0ff00,0x40ff4000,0xffd00000,
            0xc0c0c000,0x80808000,0x80000000,0x80800000,0x00008000,0x00808000,0x4000a000,0xff007000,0x00a0ff00};
    uint32_t buf[nbLeds];
    uint16_t cnt=0;                 // compteur de tours général
    uint16_t i=0;                   // ptr décalage ; avance à chaque tour
    uint8_t c=mincol;               // ptr couleur courante ; abvance tous les coloopmax tours
    uint8_t coloop=0;               // compteur de boucles de la même couleur
    uint16_t j=0;                   // ptr écriture buffer courant
    uint16_t d=0;                   // compteur leds

    //for(uint8_t x=0;x<nbcol;x++){colbuf[x]=0xff000000;}

    for(uint8_t v=0;v<nbLeds;v++){buf[v]=reduc(colbuf[c],7);}
    pio_sm_put_dma_array(pio,sm,buf,nbLeds);sleep_ms(25);
    
    while(cnt<coloopmax*nbcol){     // nbre maxi de tours 
            d++;      
            if(j==i){           // zone + lumineuse
                
                for(int16_t k=0;k<spec;k++){
                    ccur=c;if(j<i){ccur=newcol;}                                       
                    if(k<spec/4 || k>spec-4){buf[j]=reduc(colbuf[ccur],6);}
                    else if(k<(spec/2-1) || k>spec/2){buf[j]=reduc(colbuf[ccur],4);}
                    else if(k!=(spec/2-1)){buf[j]=reduc(colbuf[ccur],3);}
                    else {buf[j]=reduc(colbuf[ccur],2);}
                    j++;if(j>=nbLeds){j=0;}
                }
                c=newcol;
                if(i==nbLeds-spec){                         //  prod nlle couleur
                    coloop++;                               
                    if(coloop>=coloopmax){
                        coloop=0;
                        newcol=c+1;if(newcol>=nbcol){newcol=mincol;}
                    }
                    oldcol=c;
                }
            }
            else {
                ccur=c;
                if(j>i){ccur=oldcol;}
                buf[j]=reduc(colbuf[ccur],7);j++;if(j>=nbLeds){j=0;};}

            if(d>=nbLeds){                // buffer full
                d=0;
                pio_sm_put_dma_array(pio,sm,buf,nbLeds);
                i++;if(i>=nbLeds){
                    i=0;
                }
                j=i;
                if((j&0x000f)==0){ledblink(50);}
                else sleep_ms(50);
                //printf(" i,j,cnt:%d %d %d\n",i,j,cnt);
        
                if(i==0){    // tour terminé
                    cnt++;                  
                }
            }
        }
}

void test_ws_dma(PIO pio,int sm){    
    while(1){
        perso(pio,sm);
        perso2(pio,sm);
    }    
}


int main() {
    stdio_init_all();

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    ledblink(100);sleep_ms(10000);ledblink(100);

    printf("\n+testWs2812\n");

    /*
    // choix pio,sm
    // pour le pio0,sm0,c'est DREQ_PIO0_TX0 dans 'channel_config_set_dreq()'
    PIO pio = pio0;
    int sm = 0;

    // réservation sm 
    //sm = pio_claim_unused_sm(pio, true);
    // Charger le programme PIO
    uint offset = pio_add_program(pio, &ws2812_program);

    // Configurer le state machine
    pio_gpio_init(pio, PIN_WS2812);                                 // attache le gpio au pio (si plusieurs gpio plusieurs inits)
    pio_sm_set_consecutive_pindirs(pio, sm, PIN_WS2812, 1, true);   // 1er,nbre,direction des gpio de la sm (correspond pour le pilotage sm à "gpio_set_dir()" en pilotage processeur)

    pio_sm_config c = ws2812_program_get_default_config(offset);    // créé la structure de la config de la sm
    sm_config_set_sideset_pins(&c, PIN_WS2812);                     // gpio de base de la sm qui sera associée à la structure                 
    sm_config_set_out_pins(&c, PIN_WS2812, 1);                      // direction des GPIOs de la sm (pas compris pourquoi il y a 2 couches de direction avec pio_sm_consecutive_pindirs)                 
    sm_config_set_out_shift(&c, false, true, 24);                    // controle du shift register alimenté par le TX FIFO (,right,autopull,threshpld)
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);                  // concatène FIFO TX et RX (8 bytes)
    float div = 14;
    sm_config_set_clkdiv(&c, div);                                  // 150/14 ~ 107nS
    pio_sm_init(pio, sm, offset, &c);                               // attache le programme et la structure à la sm 
    
    //pio_sm_set_clkdiv(pio,sm, div);
    //sm_config_set_clkdiv(&c, div);

    //gpio_set_function(PIN_WS2812, GPIO_FUNC_PIO0);                  
    
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);

    init_dma_ws();

    dma_done=true;
    */
    ws_dma_setup(pio0,0);

    printf("début ws2812_dma:%d\n",dma_done);ledblink(100);

    test_ws_dma(ws_pio,ws_sm);
}




 


/*
           
    
    uint32_t up_down[lbvrj];
    uint32_t init_val[]={0x800000,0x008000,0x000080,0x008080};
    memset(up_down,0x00,lbvrj*4);
    cnt=0;
    while(cnt<4){
        uint32_t iv=init_val[cnt];
        for(int8_t i=0;i<lbvrj;i++){up_down[i]=iv>>i*2;pio_sm_put_dma_array(pio,sm,up_down,lbvrj);sleep_ms(ms);up_down[i]=0;}
        for(int8_t i=lbvrj-2;i>0;i--){up_down[i]=iv>>i*2;pio_sm_put_dma_array(pio,sm,up_down,lbvrj);sleep_ms(ms);up_down[i]=0;}
        cnt++;
    }*/


