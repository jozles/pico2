#include <stdio.h>
#include "pico/stdlib.h"
#include "const.h"
#include "coder.h"
#include "util.h"
#include "hardware/pio.h"

// led ********

#define LEDBLINK  if((millisCounter-ledBlinker)>durOffOn[led]){ledBlinker=millisCounter;led=!led;gpio_put(LED,led);}

#define LED 25 // built_in
#define LEDONDUR 100
#define LEDOFFDUR 1000

volatile uint32_t durOffOn[]={LEDOFFDUR,LEDONDUR};
volatile bool led=false;
volatile uint32_t ledBlinker=0;

uint8_t leds[NBLEDS]={LED_RED,LED_GREEN,LED_BLUE};

// I2S *********

extern PIO pio;
extern uint sm;

volatile uint32_t millisCounter=0;

volatile int32_t coder1Counter=0;
volatile int32_t coder1Counter0=0;


int main() {
    stdio_init_all();
    sleep_ms(10000);printf("+boumboum \n");
    setup();

     
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

    //uint32_t stereo = ((uint16_t)sample << 16) | (uint16_t)sample;
    //pio_sm_put_blocking(pio, sm, stereo);
}
