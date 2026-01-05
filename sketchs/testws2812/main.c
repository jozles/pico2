/* testWs2812 */

#include <stdio.h>
#include "pico/stdlib.h"
#include <stdio.h>
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"

#define PIN_WS2812 3   // GPIO3

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


int main() {
    stdio_init_all();

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    ledblink(100);sleep_ms(10000);ledblink(100);

    printf("\n+testWs2812\n");

    PIO pio = pio0;
    int sm = 0;

    sm = pio_claim_unused_sm(pio, true);
    // Charger le programme PIO
    uint offset = pio_add_program(pio, &ws2812_program);

    // Configurer le state machine
    pio_gpio_init(pio, PIN_WS2812);
    pio_sm_set_consecutive_pindirs(pio, sm, PIN_WS2812, 1, true);

    pio_sm_config c = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, PIN_WS2812);

    sm_config_set_out_pins(&c, PIN_WS2812, 1);
    sm_config_set_out_shift(&c, true, true, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    pio_sm_init(pio, sm, offset, &c);
    

    float div = 14; // (float)clock_get_hz(clk_sys) / 800000.0f;
    pio_sm_set_clkdiv(pio,sm, div);
    //sm_config_set_clkdiv(&c, div);

    gpio_set_function(PIN_WS2812, GPIO_FUNC_PIO0);
    
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);

    printf("début ws2812\n");ledblink(100);

    uint32_t ms=700;
    while (true) {
             pio_sm_put_blocking(pio,sm,0xF80000); // bleu
             pio_sm_put_blocking(pio,sm,0x0000F0); // vert
             pio_sm_put_blocking(pio,sm,0x00F800); // rouge
             pio_sm_put_blocking(pio,sm,0x00F0F0); // jaune
             sleep_ms(ms);
             
             pio_sm_put_blocking(pio,sm,0x00F0F0); // jaune
             pio_sm_put_blocking(pio,sm,0xF80000); // bleu
             pio_sm_put_blocking(pio,sm,0x0000F0); // vert
             pio_sm_put_blocking(pio,sm,0x00F800); // rouge
             sleep_ms(ms);

             pio_sm_put_blocking(pio,sm,0x00F800); // rouge
             pio_sm_put_blocking(pio,sm,0x00F0F0); // jaune
             pio_sm_put_blocking(pio,sm,0xF80000); // bleu
             pio_sm_put_blocking(pio,sm,0x0000F0); // vert
             
             sleep_ms(ms);
             pio_sm_put_blocking(pio,sm,0x0000F0); // vert
             pio_sm_put_blocking(pio,sm,0x00F800); // rouge
             pio_sm_put_blocking(pio,sm,0x00F0F0); // jaune
             pio_sm_put_blocking(pio,sm,0xF80000); // bleu
             sleep_ms(ms);
             //pio_sm_put_blocking(pio,sm,0x00000000); // off
             //sleep_ms(500);             
    }
}
 
/* 
    // Envoyer une couleur test (vert)
    while (true) {
        
        ws2812_put_pixel(pio, sm, 0x00FF00); // vert
        
        //uint32_t grb = (r << 16) | (g << 8) | b;
        //pio_sm_put_blocking(pio, sm, grb << 8);

        if((ledcnt&0x0000000f)!=0){sleep_ms(100);}
        else ledblink(100);
        ledcnt++;
    }


            grb=0x00ff00;
        pio_sm_put_blocking(pio,sm,grb); // vert
        sleep_ms(500);
        grb=0x0000FF;
        pio_sm_put_blocking(pio,sm,grb); // bleu
        sleep_ms(500);
        grb=0xFF0000;
        pio_sm_put_blocking(pio,sm,grb); // rouge
        sleep_ms(500);
        grb=0xFFFFFF;
        pio_sm_put_blocking(pio,sm,grb); // blanc
        sleep_ms(500);
        grb=0x000000;
        pio_sm_put_blocking(pio,sm,grb); // off
        sleep_ms(500);
}*/
