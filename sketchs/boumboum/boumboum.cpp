#include <stdio.h>
#include "pico/stdlib.h"
#include "const.h"
#include "coder.h"
#include "util.h"
#include "hardware/pio.h"

#include "test.h"

// leds

extern volatile uint32_t durOffOn[];
extern volatile bool led;
extern volatile uint32_t ledBlinker;

// I2S 

extern bool i2s_hungry;                 // indique que le buffer courant est copié dans le buffer de dma ; donc préparer la suite
extern int32_t* audio_data;             // pointeur du buffer courant

// millis

volatile uint32_t millisCounter=0;

// coder    

volatile int32_t coder1Counter=0;
volatile int32_t coder1Counter0=0;


int main() {
    stdio_init_all();
    sleep_ms(10000);printf("+boumboum \n");
    setup();

    //testSample(440,200);

     
    coderInit(PIO_CLOCK,PIO_DATA,PIO_SW,PIO_VPP,CODER_TIMER_POOLING_INTERVAL_MS,CODER_STROBE_NUMBER);
    coderSetup(&coder1Counter);

    while (true) {

        LEDBLINK

        if(coder1Counter!=coder1Counter0){
            printf("\r        \r%lu",coder1Counter);
            coder1Counter0=coder1Counter;    
        }
        //tight_loop_contents(); // évite l’optimisation
    }

}
