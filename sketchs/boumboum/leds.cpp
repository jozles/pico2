/* leds.cpp */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"
#include "const.h"

//static PIO pio = ws2812_pio;   // pio0 used by i2s
//static uint sm;
//static uint offset;

extern volatile uint32_t millisCounter;

/* test datas */
#define LEDS_NB 4
#define COL_NB 4
uint8_t cnt=0;
uint32_t t0=0;        
uint8_t lbvrj=COL_NB;
uint32_t bvrj[] = {0xF80000,0x0000F0,0x00F800,0x00F0F0,0xF80000,0x0000F0,0x00F800,0x00F0F0};
uint32_t up_down[LEDS_NB];   
uint32_t init_val[]={0x800000,0x008000,0x000080,0x008080};  // B R V Y

/*void ledsWs2812Setup(void) {
    printf("=== WS2812 RP2350 / GPIO%d ===\n", LED_PIN_WS2812);

    // Réservation d’un state machine et chargement du programme
    sm = pio_claim_unused_sm(pio, true);
    offset = pio_add_program(pio, &ws2812_program);

    // Initialisation du GPIO pour le PIO
    pio_gpio_init(pio, LED_PIN_WS2812);
    // Forcer la direction en sortie (sinon le pin reste en entrée → niveau bas)
    pio_sm_set_consecutive_pindirs(pio, sm, LED_PIN_WS2812, 1, true);

    // Configuration du state machine
    pio_sm_config c = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, LED_PIN_WS2812);
    sm_config_set_out_pins(&c, LED_PIN_WS2812, 1);
    sm_config_set_out_shift(&c, true, true, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    pio_sm_init(pio, sm, offset, &c);

    // Fréquence correcte pour WS2812 (~800 kHz, soit 1,25 µs par bit)
    float div = (float)clock_get_hz(clk_sys) / 800000.0f;
    pio_sm_set_clkdiv(pio, sm, div);

    // Activer le state machine
    pio_sm_set_enabled(pio, sm, true);

    printf(" +PROGRAM SIZE = %u instructions\n", ws2812_program.length);
}*/

/*static inline void ws2812_put(uint32_t grb) {
    // Envoi d’un pixel en format GRB
    pio_sm_put_blocking(pio, sm, grb);
}*/

void pio_sm_put_blocking_array(PIO pio, uint sm, const uint32_t *src, size_t len) {
    for (size_t i = 0; i < len; i++) {
        pio_sm_put_blocking(pio, sm, src[i]);
    }
}

int ledsWs2812Setup(PIO pio,uint8_t ledPin) {

    // réservation sm
    int sm = pio_claim_unused_sm(pio, true); 
    if(sm<0){
        printf("ledsWs2812Setup: no sm available\n");
        return -1;
    }
    // Charger le programme PIO
    uint offset = pio_add_program(pio, &ws2812_program);

    // Configurer le state machine
    pio_gpio_init(pio, ledPin);                                 // attache le gpio au pio (si plusieurs gpio plusieurs inits)
    pio_sm_set_consecutive_pindirs(pio, sm, ledPin, 1, true);   // 1er,nbre,direction des gpio de la sm (correspond pour le pilotage sm à "gpio_set_dir()" en pilotage processeur)

    pio_sm_config c = ws2812_program_get_default_config(offset);    // créé la structure de la config de la sm
    sm_config_set_sideset_pins(&c, ledPin);                     // gpio de base de la sm qui sera associée à la structure                 
    sm_config_set_out_pins(&c, ledPin, 1);                      // direction des GPIOs de la sm (pas compris pourquoi il y a 2 couches de direction avec pio_sm_consecutive_pindirs)                 
    sm_config_set_out_shift(&c, true, true, 24);                    // controle du shift register alimenté par le TX FIFO (,right,autopull,threshpld)
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);                  // concatène FIFO TX et RX (8 bytes)
    float div = 14;
    sm_config_set_clkdiv(&c, div);                                  // env 150/14 = 107nS
    pio_sm_init(pio, sm, offset, &c);                               // attache le programme et la structure à la sm 
    
    //pio_sm_set_clkdiv(pio,sm, div);
    //sm_config_set_clkdiv(&c, div);
    //gpio_set_function(PIN_WS2812, GPIO_FUNC_PIO0);                  
    
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);

    printf("début ws2812\n");
    return sm;
}

void ledsWs2812Test(PIO pio,int sm,uint32_t ms) {

    if((millisCounter-t0)>ms){
        t0=millisCounter;
  
        if(cnt<(LEDS_NB*2)){
            pio_sm_put_blocking_array(pio,sm,&bvrj[cnt%LEDS_NB],LEDS_NB);
            cnt=LEDS_NB*2-1;
        }
        else{
        
            uint8_t p=cnt-LEDS_NB*2;        // p 0 - COL_NB*(LEDS_NB+(LEDS_NB-2) 1 AR/coul
 
            if(p<COL_NB*(2*LEDS_NB-2)){     // montée + descente / coul 
                memset(up_down,0x00,LEDS_NB);
                uint32_t x=p/(LEDS_NB*2-2);     // x 0 - COL_NB  (0 color 0 ; 1 color 1 ... n color lbvrj)
                uint32_t i=p%(LEDS_NB*2-2);     // y 0-(LEDS_NB*2-2) position dans la couleur (montée+descente)
                uint32_t iv=init_val[x];        // iv = valeur initiale de la couleur
            
                if(i<LEDS_NB){      // montée
                    up_down[i]=iv>>i*2;pio_sm_put_blocking_array(pio,sm,up_down,lbvrj);up_down[i]=0;}
                else {              // descente
                    i=2*(LEDS_NB-1)-i; 
                    up_down[i]=iv>>i*2;pio_sm_put_blocking_array(pio,sm,up_down,lbvrj);up_down[i]=0;}  
            }
        }
        cnt++;
        if(cnt>=(LEDS_NB*2+COL_NB*(2*LEDS_NB-2))){
            cnt=0;
        }
    }
}


/*void ledsWs2812Test(void) {
    while (true) {
        ws2812_put(0x00FF00); // vert
        sleep_ms(500);
        ws2812_put(0x0000FF); // bleu
        sleep_ms(500);
        ws2812_put(0xFF0000); // rouge
        sleep_ms(500);
        ws2812_put(0xFFFFFF); // blanc
        sleep_ms(500);
        ws2812_put(0x000000); // off
        sleep_ms(500);
    }
}*/

