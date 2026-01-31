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

uint8_t curFonc=0;
volatile int32_t coderCounter[FONC_NB+MAX_FONC];        // datas
volatile int32_t coderCounter0[FONC_NB+MAX_FONC];
#endif // MUXED_CODER

// voices 

uint8_t currVoice=0;
struct Voice voices[VOICES_NB];

// frequence

volatile int16_t freq_lin=0;
volatile int16_t amplitude=0;   

// ws2812

static PIO pioWs = ws2812_pio;   // pio0 used by i2s




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

    while(1){       

        ws_show_3(30);

        //test_st7789(1000);

        LEDBLINK
       
    }    

    while (true) {

        LEDBLINK

#ifndef MUXED_CODER
        if(coder1Counter!=coder1Counter0){

            coder1Counter0=coder1Counter;
            voices[0].newFrequency=calcFreq(coder1Counter);

            printf("freq:%5.3f ampl:%d   \r",voices[0].frequency,voices[0].genAmpl);           
        }
    }
#endif  // MUXED_CODER 

#endif  // BB_TEST_MODE

#ifndef BB_TEST_MODE

        // inits 
        for(uint8_t v=0;v<VOICES_NB_NB,v++){        
            coderCounter[v]=0;                  // voices mixer loop
        }
        for(uint8_t f=0;f<=MAX_FONC,f++){
            coderCounter[f+v]=0;                // voices params (spectrum_mixer, adsr, ...)
        }
        

        currFonc=VOICE0_SPECTRUM_MIXER;
        currVoice=0;

        while(1){
            tft_show(currFonc,currVoice);
            coderSetup(&coderCounter[currFonc]);
        }

#endif  // BB_TEST_MODE

}
