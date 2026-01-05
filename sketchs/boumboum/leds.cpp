/* leds.cpp */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"
#include "const.h"

static PIO pio = pio0;   // si GPIO3 n’est pas routé sur pio0, bascule sur pio1
static uint sm;
static uint offset;

void ledsWs2812Setup(void) {
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
}

static inline void ws2812_put(uint32_t grb) {
    // Envoi d’un pixel en format GRB
    pio_sm_put_blocking(pio, sm, grb);
}



void ledsWs2812Test(void) {
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
}

