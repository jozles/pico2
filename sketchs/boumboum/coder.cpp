#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "const.h"
#include "coder.h"
#include "util.h"



uint16_t coderTimerPoolingInterval=1;       // delay betxeen Its (mS) changed by init
uint8_t coderStrobeNumber=3;                // 1st strobe delay (2nd strobe delay is 1)
volatile int32_t* coderTimerCount=nullptr;  // ptr to current value to be inc or dec
uint32_t coderTimer=0;                      // It counter
uint8_t coderItStatus=0;                    // coder decoding status
bool coderClock=0;                          // current physical coder clock value
bool coderClock0=0;                         // previous physical coder clock value
bool coderData=0;                           // current physical coder data value
bool coderData0=0;                          // previous physical coder data value
bool coderSwitch=0;                         // current physical coder switch value

// pico2_pins

uint8_t pio_clock_pin;
uint8_t pio_data_pin;
uint8_t pio_switch_pin;


uint8_t ledptr=0;
extern uint8_t leds[];



bool coderTimerHandler(struct repeating_timer *t){

/*if(coderTimer/256*256==coderTimer){                             // check Interrupt
    digitalWrite(LED,!digitalRead(LED));
}*/

    coderTimer++;

    coderClock=gpio_get(pio_clock_pin);

    if(coderClock == coderClock0){                                // no change 
        if(coderItStatus<coderStrobeNumber){                      // wait for change after strobe delay
            coderItStatus++;return true;}
        if(coderItStatus>coderStrobeNumber){                      // 2nd strobe fail
            coderItStatus=0;return true;}
        return true;
    }
    else{                                                         // clock change detected 
        coderData=gpio_get(pio_data_pin);                         // latch data

        if(coderItStatus<coderStrobeNumber){                      // change to close to previous valid one : ignore it
            coderItStatus=0;return true;}
                                                            
        if(coderItStatus==coderStrobeNumber){     
            coderItStatus++;return true;}                              // 1st strobe passed wait next It
    
    }
 
    coderClock0=coderClock;                                       // valid clock change detected after 2 strobes delay
    coderItStatus=0;

    if(coderTimerCount!=nullptr){

        coderSwitch=gpio_get(pio_switch_pin);

        if((!coderClock)^coderData){
            (*coderTimerCount)-=1+coderSwitch;
        } 
        else {
            (*coderTimerCount)+=1+coderSwitch;
        }
    }

    // here accelerator management could be added
    
    return true;    // relancer le timer
}

void coderInit(uint8_t pio_ck,uint8_t pio_d,uint8_t pio_sw,uint16_t ctpi,uint8_t cstn){
    
    pio_clock_pin=pio_ck;
    pio_data_pin=pio_d;
    pio_switch_pin=pio_sw;

    coderTimerPoolingInterval=ctpi;
    coderStrobeNumber=cstn;

    coderTimer=0;
    coderItStatus=0;

    gpio_set_dir(pio_data_pin,GPIO_IN); 
    gpio_set_dir(pio_clock_pin,GPIO_IN);
    gpio_set_dir(pio_switch_pin,GPIO_IN);

    coderClock0=gpio_get(pio_clock_pin);
    coderData0=gpio_get(pio_data_pin);

    struct repeating_timer timer;
    add_repeating_timer_ms(1, coderTimerHandler, NULL, &timer);

}

void coderSetup(volatile int32_t* cTC){
    coderTimerCount=cTC;}
