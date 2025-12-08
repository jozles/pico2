#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"
#include "i2s.pio.h"   // Fichier généré par pioasm (programme I²S)
#include <math.h>
#include <stdio.h>

#define SAMPLE_RATE 22050 // 44100
#define FREQUENCY   440.0   // La 440 Hz
#define AMPLITUDE   30000   // Amplitude max (16 bits signé)

#define I2S_DATA_PIN  4     // DIN du MAX98357A
#define I2S_BCLK_PIN  2     // BCLK
#define I2S_LRCLK_PIN 3     // LRCLK

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Led
#define LED 25 // built_in
#define LEDONDUR 100
#define LEDOFFDUR 1000

volatile uint32_t durOffOn[]={LEDOFFDUR,LEDONDUR};
uint32_t dur;
volatile bool led=false;

volatile uint32_t millisCounter=0;
volatile uint32_t ledBlinker=0;


// timer interrupt handler
struct repeating_timer millisTimer;


bool millisTimerHandler(struct repeating_timer *t){
    //if(millisCounter>2000){gpio_put(LED,true);millisCounter=0;}
    //if(millisCounter>1000){gpio_put(LED,false);}
    millisCounter++;
    return true;
}

// inits
void setup(){
    sleep_ms(10000);
    printf("+sine \n");
    gpio_get(led);printf("-led %d\n",led);
    gpio_init(LED);gpio_set_dir(LED,GPIO_OUT);gpio_put(LED,false);
    gpio_put(LED,true);sleep_ms(1000);gpio_put(LED,false);
    printf("-timer \n");
    add_repeating_timer_ms(1, millisTimerHandler, NULL, &millisTimer);
    sleep_ms(1000);printf("led %d %lu %lu\n",gpio_get(LED),millisCounter,ledBlinker);
}

void i2s_program_init(PIO pio, uint sm, uint offset,
                      uint pin_data, uint pin_bclk, uint pin_lrclk,
                      uint sample_rate) {
    // Configurer les pins
    pio_gpio_init(pio,pin_data);
    pio_gpio_init(pio,pin_bclk);
    pio_gpio_init(pio,pin_lrclk);

    pio_sm_set_consecutive_pindirs(pio, sm, pin_data, 1, true);
    pio_sm_set_consecutive_pindirs(pio, sm, pin_bclk, 1, true);
    pio_sm_set_consecutive_pindirs(pio, sm, pin_lrclk, 1, true);

    // Charger le programme
    pio_sm_config c = i2s_program_get_default_config(offset);

    // Associer les pins
    sm_config_set_out_pins(&c, pin_data, 1);
    sm_config_set_sideset_pins(&c, pin_bclk);

    // Clock divider pour atteindre sample_rate
    float div = (float)clock_get_hz(clk_sys) / (sample_rate * 32); 
    sm_config_set_clkdiv(&c, div);

    // Init state machine
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}


int main() {
    stdio_init_all();
    setup();

    // I²S load
    PIO pio = pio0;
    uint offset = pio_add_program(pio, &i2s_program);
    uint sm = pio_claim_unused_sm(pio, true);

    i2s_program_init(
        pio, sm, offset,
        I2S_DATA_PIN, I2S_BCLK_PIN, I2S_LRCLK_PIN,
        SAMPLE_RATE);

    // Sine gen
    double phase = 0.0;
    double phase_inc = 2.0 * M_PI * FREQUENCY / SAMPLE_RATE;

    printf("led %d %lu %lu\n",gpio_get(LED),millisCounter,ledBlinker);

    while (true) {

        // blink
        if((millisCounter-ledBlinker)>durOffOn[led]){
            ledBlinker=millisCounter;
            led=!led;
            gpio_put(LED,led);
        }

    //}
    ///*
        int16_t sample = (int16_t)(AMPLITUDE * sin(phase));
        phase += phase_inc;
        if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;

        uint32_t stereo = ((uint16_t)sample << 16) | (uint16_t)sample;
        pio_sm_put_blocking(pio, sm, stereo);
    }//*/
}
