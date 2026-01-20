/* testWs2812 */

#include <stdio.h>
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"

#define PIN_WS2812 0   // GPIO3

uint32_t ledcnt=0;

void ledblink(uint16_t t)
{
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    sleep_ms(t);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
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

int main() {
    stdio_init_all();

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    ledblink(100);sleep_ms(10000);ledblink(100);

    printf("\n+testWs2812\n");

    
    // choix pio
    PIO pio = pio0;
    int sm = 0;

    // réservation sm 
    sm = pio_claim_unused_sm(pio, true);
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
    sm_config_set_clkdiv(&c, div);                                  // env 150/14 = 107nS
    pio_sm_init(pio, sm, offset, &c);                               // attache le programme et la structure à la sm 
    
    //pio_sm_set_clkdiv(pio,sm, div);
    //sm_config_set_clkdiv(&c, div);

    //gpio_set_function(PIN_WS2812, GPIO_FUNC_PIO0);                  
    
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);

    printf("début ws2812\n");ledblink(100);

    uint32_t ms=700;
    uint8_t lbvrj=4;
    uint32_t bvrj[] = {0xF80000,0x0000F0,0x00F800,0x00F0F0,0xF80000,0x0000F0,0x00F800,0x00F0F0};
    uint8_t cnt=0;
while(1){
    cnt=0;    
    while(cnt<2){
        cnt++;
        for(uint8_t i=0;i<lbvrj;i++){pio_sm_put_blocking_array(pio,sm,&bvrj[i],lbvrj);sleep_ms(ms);}
    }
    uint32_t up_down[lbvrj];
    uint32_t init_val[]={0x800000,0x008000,0x000080,0x008080};
    memset(up_down,0x00,lbvrj*4);
    cnt=0;
    while(cnt<4){
        uint32_t iv=init_val[cnt];
        for(int8_t i=0;i<lbvrj;i++){up_down[i]=iv>>i*2;pio_sm_put_blocking_array(pio,sm,up_down,lbvrj);sleep_ms(ms);up_down[i]=0;}
        for(int8_t i=lbvrj-2;i>0;i--){up_down[i]=iv>>i*2;pio_sm_put_blocking_array(pio,sm,up_down,lbvrj);sleep_ms(ms);up_down[i]=0;}
        cnt++;
    }
}

}