#include <stdio.h>
#include "pico/stdlib.h"
#include "const.h"
#include "coder.h"
#include "util.h"
#include "hardware/pio.h"

#include "test.h"
#include "frequences.h"

// leds

extern volatile uint32_t durOffOn[];
extern volatile bool led;
extern volatile uint32_t ledBlinker;

// millis

volatile uint32_t millisCounter=0;

// coder    

volatile int32_t coder1Counter=0;
volatile int32_t coder1Counter0=0;

// frequence

volatile int16_t freq_lin=0;
volatile int16_t amplitude=0;   

struct voice voices[VOICES_NB];


int main() {
    stdio_init_all();
    sleep_ms(10000);printf("\n+boumboum \n");
    setup();

    voices[0].basicWaveAmpl[WAVE_SINUS]=MAX_AMP_VAL;
    voices[0].genAmpl=6000;
    freq_lin=1943;      // 440Hz

    coder1Counter=freq_lin;



uint32_t sb[SAMPLE_BUFFER_SIZE];  


    while (true) {

        LEDBLINK

        if(coder1Counter!=coder1Counter0){
            //printf("\r        \r%lu",coder1Counter);
            coder1Counter0=coder1Counter;
            voices[0].frequency=calcFreq(coder1Counter);

            printf("freq:%5.3f ampl:%d\r",voices[0].frequency,voices[0].genAmpl);
            
            //testSample(coder1Counter,amplitude);    
        }
        //tight_loop_contents(); // évite l’optimisation

           
    }

}
