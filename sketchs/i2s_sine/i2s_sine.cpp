/*

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "i2s.pio.h"

#define SAMPLE_RATE 44100

int main() {
    stdio_init_all();
    PIO pio = pio0;
    uint sm = 0;

    // Charger programme PIO
    uint offset = pio_add_program(pio, &i2s_program);
    pio_sm_config c = i2s_program_get_default_config(offset);

    // Config pins
    pio_gpio_init(pio, 2); // BCLK
    pio_gpio_init(pio, 3); // LRCLK
    pio_gpio_init(pio, 4); // DIN
    sm_config_set_sideset_pins(&c, 2);
    sm_config_set_out_pins(&c, 4, 1);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    // Fréquence
    uint32_t sys_hz = clock_get_hz(clk_sys);
    float clkdiv = (float)sys_hz / (SAMPLE_RATE * 32);
    sm_config_set_clkdiv(&c, clkdiv);

    // Init et démarrage
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);

    // Envoie un mot constant (gauche=0x7FFF, droit=0x0000)
    while (true) {
        uint32_t sample = (0x7FFF << 16) | 0x0000;
        pio_sm_put_blocking(pio, sm, sample);
    }
}

*/


/*#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"

#include "i2s.pio.h"

#define SAMPLE_RATE 44100
#define BUFFER_SIZE 256

uint32_t audio_buffer[BUFFER_SIZE];

void fill_triangle_buffer() {
    int16_t val = -32768;
    int16_t step = 512; // vitesse de montée/descente
    for (int i = 0; i < BUFFER_SIZE; i++) {
        val += step;
        if (val > 30000 || val < -30000) step = -step;
        audio_buffer[i] = ((uint16_t)val << 16) | (uint16_t)val; // gauche=droit
    }
}

int main() {
    stdio_init_all();
    PIO pio = pio0;
    uint sm = 0;

    // Charger programme PIO
    uint offset = pio_add_program(pio, &i2s_program);
    pio_sm_config c = i2s_program_get_default_config(offset);

    // Config pins
    pio_gpio_init(pio, 2); // BCLK
    pio_gpio_init(pio, 3); // LRCLK
    pio_gpio_init(pio, 4); // DIN
    sm_config_set_sideset_pins(&c, 2);
    sm_config_set_out_pins(&c, 4, 1);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    // Fréquence
    uint32_t sys_hz = clock_get_hz(clk_sys);   // fréquence système
    float clkdiv = (float)sys_hz / (SAMPLE_RATE * 32);

    sm_config_set_clkdiv(&c, clkdiv);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);

    // Remplir buffer
    fill_triangle_buffer();

    // Config DMA
    int dma_chan = dma_claim_unused_channel(true);
    dma_channel_config dcfg = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dcfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dcfg, true);
    channel_config_set_write_increment(&dcfg, false);
    channel_config_set_dreq(&dcfg, pio_get_dreq(pio, sm, true));

    dma_channel_configure(
        dma_chan,
        &dcfg,
        &pio->txf[sm],        // destination = FIFO PIO
        audio_buffer,         // source = buffer
        BUFFER_SIZE,          // nombre de mots
        true                  // démarrer immédiatement
    );

    // Boucle infinie : relancer DMA en mode circulaire
    while (true) {
        if (!dma_channel_is_busy(dma_chan)) {
            dma_channel_set_read_addr(dma_chan, audio_buffer, true);
        }
    }
}


*/

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"
#include "i2s.pio.h"   // Fichier généré par pioasm (programme I²S)
#include <math.h>
#include <stdio.h>

#define SAMPLE_RATE 44100
#define FREQUENCY   440.0   // La 440 Hz
#define AMPLITUDE   30000   // Amplitude max (16 bits signé)

#define I2S_DATA_PIN  4     // DIN du MAX98357A
#define I2S_BCLK_PIN  2     // BCLK
#define I2S_LRCLK_PIN 3     // LRCLK

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Led
#define LEDBLINK  if((millisCounter-ledBlinker)>durOffOn[led]){ledBlinker=millisCounter;led=!led;gpio_put(LED,led);}

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
    millisCounter++;
    return true;
}

// inits
void setup(){
    sleep_ms(10000);printf("+sine \n");
    gpio_init(LED);gpio_set_dir(LED,GPIO_OUT);gpio_put(LED,false);
    gpio_get(led);printf("-led %d",led);
    gpio_put(LED,true);sleep_ms(1000);gpio_put(LED,false);
    printf(" -timer ");
    add_repeating_timer_ms(1, millisTimerHandler, NULL, &millisTimer);
    sleep_ms(1000);printf("-millis %lu \n",millisCounter);
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
    //float div = (float)clock_get_hz(clk_sys) / (sample_rate * 32); 
    //printf("clkdiv: %f\n", div);
    uint32_t sys_hz = clock_get_hz(clk_sys);  // ex. Pico 2 ~150000000 Hz
printf("sys_hz: %u\n", sys_hz);

float div = (float)sys_hz *0.9615/ (SAMPLE_RATE * 100);
printf("clkdiv: %f\n", div);

//div=33.95;
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

     //uint32_t stereo=0;
     //int32_t step=100;

 /*while (true) {

        LEDBLINK


       int16_t sample = (int16_t)(AMPLITUDE * sin(phase));
        phase += phase_inc;
        if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;

        uint32_t stereo = ((uint16_t)sample << 16) | (uint16_t)sample;

        pio_sm_put_blocking(pio,sm,stereo);*/

        //stereo+=step;
        //if(stereo>0x7fff){step*=-1;stereo+=step;}
        //if(stereo<0x0100){step*=-1;}
        //pio_sm_put_blocking(pio, sm, stereo);

    //}

#define SAMPLE_RATE 44100
#define STEPS       32
#define AMPLITUDE   30000

// Pré-calcul du tableau de 32 valeurs
int16_t table[STEPS];
for (int i = 0; i < STEPS; i++) {
    // Rampe de -AMPLITUDE à +AMPLITUDE
    table[i] = (int16_t)(-AMPLITUDE + (2.0 * AMPLITUDE * i) / (STEPS - 1));
}

// Incrément de phase : avance d’un pas de table à chaque échantillon
int step_index = 0;

while (true) {
    int16_t sample = table[step_index];
    uint32_t stereo = ((uint16_t)sample << 16) | (uint16_t)sample;
    pio_sm_put_blocking(pio, sm, stereo);

    // Avance dans la table
    step_index++;
    if (step_index >= STEPS) step_index = 0;
}

}