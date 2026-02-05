#include <stdio.h>
#include "pico/stdlib.h"
#include "const.h"
#include "coder.h"
#include "util.h"
#include "hardware/pio.h"

#include "test.h"
#include "frequences.h"
#include "leds.h"
#include "st7789.h"

// leds

extern volatile uint32_t durOffOn[];
extern volatile bool led;
extern volatile uint32_t ledBlinker;

// millis

volatile uint32_t millisCounter=0;

// coder    

#ifndef MUXED_CODER
volatile int32_t coder1Counter=0;
volatile int32_t coder1Counter0=0;
#endif // MUXED_CODER
#ifdef MUXED_CODER
Coders ct[CODER_NB];                            // cinematic

uint8_t currFonc=0;
uint32_t* currCoderBank=nullptr;
uint32_t* currCoderBank0=nullptr;
volatile int32_t coderCounter[CODER_COUNTERS];        // datas
volatile int32_t coderCounter0[CODER_COUNTERS];
#endif // MUXED_CODER

// voices 

uint8_t currVoice=0;
struct Voice voices[VOICES_NB];

// frequence

volatile int16_t freq_lin=0;
volatile int16_t amplitude=0;   

// ws2812

static PIO pioWs = ws2812_pio;   // pio0 used by i2s

// debug

volatile uint32_t dma_tfr_count=0;


int main() {
    stdio_init_all();
    sleep_ms(10000);printf("\n+boumboum \n");

    gpio_init(LED);gpio_set_dir(LED,GPIO_OUT); gpio_put(LED,LOW);
    
    setup();

#ifdef BB_TEST_MODE

    coderSetup(&coder1Counter);

    voices[0].basicWaveAmpl[WAVE_SINUS]=MAX_AMP_VAL;
    voices[0].genAmpl=6000;
    freq_lin=1943;      // 440Hz

    coder1Counter=freq_lin;
    voices[0].frequency=calcFreq(coder1Counter);   

    while (1) {

        ws_show_3(30);

        test_st7789(1000);

        LEDBLINK

#ifndef MUXED_CODER
        if(coder1Counter!=coder1Counter0){

            coder1Counter0=coder1Counter;
            voices[0].newFrequency=calcFreq(coder1Counter);

            tft_draw_text_12x12_dma_mult(0,12,"coder:", 0xFFFF, 0x0000,1);
            tft_draw_int_12x12_dma_mult(74,12,0xffff,0x0000,2,coder1Counter);

            printf("freq:%5.3f ampl:%d   \r",voices[0].frequency,voices[0].genAmpl);           
        }
    }
#endif  // MUXED_CODER 

#endif  // BB_TEST_MODE

#ifndef BB_TEST_MODE

        // inits 

        for(uint8_t f=0;f<CODER_NB,f++){
            coderCounter[VOICES_MIXER*CODER_NB+f]=MAX_16B_LINEAR_VALUE/CODER_NB;
            coderCounter[VOICE0_SPECTRUM_MIXER*CODER_NB+f]=MAX_16B_LINEAR_VALUE/CODER_NB;
            coderCounter[VOICE0_ADSR*CODER_NB+f]=MAX_16B_LINEAR_VALUE/CODER_NB;
        }

        currFonc=VOICE0_SPECTRUM_MIXER;
        currVoice=0;

        while(1){
            tft_show(currFonc,currVoice);

            currCoderBank=&coderCounter[currFonc*CODER_NB];
            currCoderBank0=&coderCounter0[currFonc*CODER_NB];

            coderSetup(currCoderBank);
            
            if(currFonc==VOICES_MIXER){autoMixer(currCoderBank,currCoderBank0);}
            if(currFonc<VOICE0_SPECTRUM_MIXER+VOICES_NB){autoMixer(currCoderBank,currCoderBank0);}
            if(currFonc<VOICE0_ADSR+VOICES_NB){adsr(currCoderBank,currCoderBank0);}
        }

#endif  // BB_TEST_MODE

}
