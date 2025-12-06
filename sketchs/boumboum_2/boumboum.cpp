#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "const.h"
#include "coder.h"
#include "util.h"

uint8_t leds[NBLEDS]={LED_RED,LED_GREEN,LED_BLUE};

uint32_t portD=  PIO_PDSR_D ;

volatile int32_t coder1Counter=0;
int32_t coder1Counter0=0;

uint32_t intDur;

int main() {
    stdio_init_all();
    initLeds();
    coderInit(PIO_PDSR_D,PIO_CLOCK,PIO_DATA,PIO_SW,PIN_CODER_A,PIN_CODER_B,PIN_CODER_C,PIN_CODER_GND,PIN_CODER_VCC,CODER_TIMER_POOLING_INTERVAL_MS,CODER_STROBE_NUMBER);
    coderSetup(&coder1Counter);


    // Boucle principale vide : tout est géré par l’interruption
    while (true) {
        ledBlink();
        if(coder1Counter!=coder1Counter0){
            printf("\r        \r%lu",coder1Counter);
            coder1Counter0=coder1Counter;    
        }
        //tight_loop_contents(); // évite l’optimisation
    }
}
