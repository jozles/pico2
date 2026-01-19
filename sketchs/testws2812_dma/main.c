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

#define PIN_WS2812 0   // GPIO3

#define NB_LEDS 70

static int dma_chan;
static dma_channel_config dma_cfg;
static volatile bool dma_done = false;

uint32_t ledcnt=0;

void ledblink(uint16_t t)
{
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    sleep_ms(t);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
}

void dma_irq_handler() {
    dma_hw->ints0 = 1u << dma_chan;   // clear IRQ

    dma_done = true;
}

void init_dma_ws() {
    dma_chan = dma_claim_unused_channel(true);
    printf("dma_chan = %d\n", dma_chan);
    dma_cfg = dma_channel_get_default_config(dma_chan);

    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, true);
    channel_config_set_write_increment(&dma_cfg, false);
    channel_config_set_dreq(&dma_cfg, DREQ_PIO0_TX0);                   // voir commentaire dans main

    dma_channel_set_irq0_enabled(dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);
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
    dma_channel_configure(dma_chan,&dma_cfg,&pio->txf[sm],src,len,true);   
}

void larson(PIO pio, uint sm) {
    uint16_t cnt=0;
    const int N = NB_LEDS;
    static uint32_t buf[NB_LEDS];
    int pos = 0;
    int dir = 1;

    while (cnt<1000) {
        for (int i = 0; i < N; i++) {
            int d = abs(i - pos);
            uint8_t intensity = d == 0 ? 255 : (d == 1 ? 80 : (d == 2 ? 20 : 0));       // 255
            buf[i] = ((intensity << 16) | 0x00 | 0x00) << 8; // rouge
        }

        pio_sm_put_dma_array(pio, sm, buf, N);

        pos += dir;
        if (pos == N - 1 || pos == 0) dir = -dir;

        sleep_ms(20);
        cnt++;
    }
}

static inline uint32_t hsv_to_grb(uint16_t h) {
    uint8_t r, g, b;
    uint8_t region = h / 43;
    uint8_t remainder = (h - (region * 43)) * 6;

    uint8_t p = 0;
    uint8_t q = (255 * (255 - remainder)) >> 8;
    uint8_t t = (255 * remainder) >> 8;

    uint8_t mxL=255;

    switch (region) {
        case 0: r = mxL; g = t;   b = 0;   break;
        case 1: r = q;   g = mxL; b = 0;   break;
        case 2: r = 0;   g = mxL; b = t;   break;
        case 3: r = 0;   g = q;   b = mxL; break;
        case 4: r = t;   g = 0;   b = mxL; break;
        default:r = mxL; g = 0;   b = q;   break;
    }

    return ((g << 16) | (r << 8) | b) << 8;   // GRB << 8 pour ton PIO
}

void rainbow_test(PIO pio, uint sm) {
    uint16_t cnt=0;
    
    const int N = NB_LEDS;
    static uint32_t buf[NB_LEDS];
    uint16_t phase = 0;

    while (cnt<1000) {
        for (int i = 0; i < N; i++) {
            buf[i] = hsv_to_grb((phase + i * 5) % 255);
        }

        pio_sm_put_dma_array(pio, sm, buf, N);
        phase++;
        sleep_ms(20);

        cnt++;
    }
}

static inline uint32_t make_grb(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t grb = (g << 16) | (r << 8) | b;
    return grb << 8;   // alignement pour ton PIO
}

void breathing_wave(PIO pio, uint sm) {
    
    uint16_t cnt=0;
    
    const int N = NB_LEDS;
    static uint32_t buf[NB_LEDS];
    float t = 0;

    while (cnt<1000) {
        for (int i = 0; i < N; i++) {
            float v = (sinf(t + i * 0.25f) + 1.0f) * 0.5f; // 0..1
            uint8_t b = (uint8_t)(v * 255.0f);
            buf[i] = make_grb(0, 0, b);  // bleu
        }

        pio_sm_put_dma_array(pio, sm, buf, N);
        t += 0.15f;

        sleep_ms(30);

        cnt++;
    }
}

void perso(PIO pio, uint sm){
    uint32_t ms=700;
    uint8_t lbvrj=4;
    uint32_t bvrj[] = {0xF80000,0x0000F0,0x00F800,0x00F0F0,0xF80000,0x0000F0,0x00F800,0x00F0F0};
    uint8_t cnt=0;

    cnt=0;    
    while(cnt<4){
        pio_sm_put_dma_array(pio,sm,bvrj+cnt,lbvrj);sleep_ms(ms);cnt++;
    }
} 

uint32_t reduc(uint32_t col,uint8_t reduc){
    uint32_t col1=(col << (reduc))&0x0000ff00;
    uint32_t col2=(col << (reduc))&0x00ff0000;
    uint32_t col3=(col << (reduc))&0xff000000;
    return (col3 | col2 | col1);
}

void perso2(PIO pio, uint sm){
    uint16_t cntMax=100;
    uint16_t coloopmax=4;
    uint16_t nbLeds=64;
    uint16_t maskNbLeds=0x003f;
    uint8_t nbcol=24;
    uint8_t mincol=2;
    uint32_t colbuf[]={0x000000,0xffffff,0xff0000,0x00ff00,0x0000ff,0xffff00,0x00ffff,0xff00ff,0xff8000,0x8000ff,0xff40a0,0x840ffe0,0x40a0ff,
        0x40ff40,0xffd000,0xc0c0c0,0x808080,0x800000,0x808000,0x000080,0x008080,0x4000a0,0xff0070,0x00a0ff};
    uint32_t buf[nbLeds];
    
    uint16_t cnt=0;                 // compteur de tours général
    uint16_t i=0;                    // ptr décalage ; avance à chaque tour
    uint8_t c=mincol;                    // ptr couleur courante ; abvance tous les coloopmax tours
    uint8_t coloop=0;               // compteur de boucles de la même couleur
    uint16_t j=0;                   // ptr écriture buffer courant

    for(uint8_t v=0;v<nbLeds;v++){buf[v]=reduc(colbuf[c],7);}
    pio_sm_put_dma_array(pio,sm,buf,nbLeds);sleep_ms(25);
    
    while((cnt/nbLeds)<cntMax){              // nbre maxi de tours 
                  
            if(j==i){
                for(uint16_t k=0;k<16;k++){
                    // zone + lumineuse
                    if(k<3 || k>12){buf[j]=reduc(colbuf[c],5);}
                    else if(k<6 || k>8){buf[j]=reduc(colbuf[c],3);}
                    else if(k!=7){buf[j]=reduc(colbuf[c],1);}
                    else {buf[j]=reduc(colbuf[c],0);}
                    j++;j&=maskNbLeds;
                }
            }
            else {buf[j]=reduc(colbuf[c],7);j++;j&=maskNbLeds;}
            
            if(j==0){                // buffer full
                pio_sm_put_dma_array(pio,sm,buf,nbLeds);
                i++;i&=maskNbLeds;
                sleep_ms(25);
                cnt++;
                printf(" i,j,cnt:%d,%d %d\n",i,j,cnt);
            }
            if(cnt&maskNbLeds==0){    // tour terminé
                coloop++;
                if(coloop>=coloopmax){
                    coloop=0;
                    c++;if(c>=nbcol){c=mincol;}
                }
            }
    }
}



int main() {
    stdio_init_all();

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    ledblink(100);sleep_ms(10000);ledblink(100);

    printf("\n+testWs2812\n");

    
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
    sm_config_set_out_shift(&c, true, true, 24);                    // controle du shift register alimenté par le TX FIFO (,right,autopull,threshpld)
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

    printf("début ws2812_dma:%d\n",dma_done);ledblink(100);

    while(1){
        //larson(pio,sm);
        //perso(pio,sm);
        //breathing_wave(pio,sm);
        //perso(pio,sm);
        //rainbow_test(pio,sm);
        perso(pio,sm);
        perso2(pio,sm);
    }    
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


