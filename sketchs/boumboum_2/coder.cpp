#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <vector>


#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "coder.h"  
#include "const.h"

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
// due_pins
volatile uint32_t pio_port;
uint8_t pio_clock_pin;
uint8_t pio_data_pin;
uint8_t pio_switch_pin;
// arduino_pins
//uint8_t arduino_clock_pin;
//uint8_t arduino_data_pin;
//uint8_t arduino_switch_pin;

uint8_t ledptr=0;
extern uint8_t leds[];



bool coderTimerHandler(struct repeating_timer *t){

/*if(coderTimer/256*256==coderTimer){                             // check Interrupt
    digitalWrite(LED,!digitalRead(LED));
}*/

    coderTimer++;
    //coderClock= (pio_port & (1<<pio_clock_bit)) !=0 ;
    coderClock=gpio_get(pio_clock_pin);

    if(coderClock == coderClock0){                                // no change 
        if(coderItStatus<coderStrobeNumber){                      // wait for change after strobe delay
            coderItStatus++;return true;}
        if(coderItStatus>coderStrobeNumber){                      // 2nd strobe fail
            coderItStatus=0;return true;}
        return true;
    }
    else{                                                         // clock change detected 
        coderData=gpio_get(pio_data_pin);                  // latch data

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

void coderInit(volatile uint32_t port,uint8_t pio_ck,uint8_t pio_d,uint8_t pio_sw,uint8_t ground_pin,uint8_t vcc_pin,uint16_t ctpi,uint8_t cstn){
    
    pio_port=port;
    pio_clock_pin=pio_ck;
    pio_data_pin=pio_d;
    pio_switch_pin=pio_sw;
    //arduino_clock_pin=clock_pin;
    //arduino_data_pin=data_pin;
    //arduino_switch_pin=switch_pin;

    coderTimerPoolingInterval=ctpi;
    coderStrobeNumber=cstn;

    coderTimer=0;
    coderItStatus=0;

    gpio_set_dir(pio_data_pin,GPIO_IN);   // 25 coder A  data
    gpio_set_dir(pio_clock_pin,GPIO_IN);   // 27 coder C  clock
    gpio_set_dir(pio_switch_pin,GPIO_IN);   // 26 coder B  switch

    //pinMode(ground_pin,OUTPUT);digitalWrite(23,LOW);       // coder ground
    gpio_set_dir(ground_pin, GPIO_OUT);gpio_put(ground_pin, LOW);
    gpio_set_dir(vcc_pin,GPIO_OUT);gpio_put(vcc_pin,HIGH);      // coder Vcc 

    //coderClock0= (pio_port & (1<<pio_clock_bit)) !=0 ;
    coderClock0=gpio_get(pio_clock_pin);
    //coderData0= (pio_port & (1<<pio_data_bit)) !=0 ;
    coderData0=gpio_get(pio_data_pin);

    struct repeating_timer timer;
    add_repeating_timer_ms(1, coderTimerHandler, NULL, &timer);
    //attachDueInterrupt(coderTimerPoolingInterval*1000, coderTimerHandler, "coderTimer");
}

void coderSetup(volatile int32_t* cTC){
    coderTimerCount=cTC;}
