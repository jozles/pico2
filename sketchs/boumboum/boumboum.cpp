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




int main() {
    stdio_init_all();
    sleep_ms(10000);printf("\n+boumboum \n");
    setup();

    amplitude=500;
    freq_lin=1943;      // 440Hz

    coder1Counter=freq_lin;

    


    while (true) {

        LEDBLINK

        if(coder1Counter!=coder1Counter0){
            //printf("\r        \r%lu",coder1Counter);
            coder1Counter0=coder1Counter;
            testSample(coder1Counter,amplitude);    
        }
        //tight_loop_contents(); // évite l’optimisation

           
    }

}
