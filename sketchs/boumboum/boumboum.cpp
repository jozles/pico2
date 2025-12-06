#include <stdio.h>
#include "pico/stdlib.h"
//#include "hardware/timer.h"
#include "const.h"
#include "coder.h"
#include "util.h"

uint8_t leds[NBLEDS]={LED_RED,LED_GREEN,LED_BLUE};

volatile int32_t coder1Counter=0;
int32_t coder1Counter0=0;

uint32_t intDur;

uint32_t testCnt=0;

extern uint32_t millisCounter;

bool millisTimerHandler(struct repeating_timer *t){
    millisCounter++;
    return true;
}

int main() {
    stdio_init_all();
    initLeds();

    struct repeating_timer millisTimer;
    add_repeating_timer_ms(10, millisTimerHandler, NULL, &millisTimer);

     
    //coderInit(PIO_CLOCK,PIO_DATA,PIO_SW,CODER_TIMER_POOLING_INTERVAL_MS,CODER_STROBE_NUMBER);
    //coderSetup(&coder1Counter);


    // Boucle principale vide : tout est géré par l’interruption
    while (true) {

    if(millisCounter>=1000){
    millisCounter=0;gpio_put(LED,ON);sleep_ms(3000);
    gpio_put(LED,false);sleep_ms(1000);}

            gpio_put(LED,true);sleep_ms(30);
            gpio_put(LED,false);sleep_ms(50);
            gpio_put(LED,HIGH);sleep_ms(30);
            gpio_put(LED,LOW);sleep_ms(1500);
        /*
        ledBlink();
        if(coder1Counter!=coder1Counter0){
            printf("\r        \r%lu",coder1Counter);
            coder1Counter0=coder1Counter;    
        }
        //tight_loop_contents(); // évite l’optimisation
    */
    }
}
