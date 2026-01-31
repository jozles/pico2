#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/pio.h"
#include "const.h"
#include "coder.h"
#include "util.h"


#ifndef MUXED_CODER
uint8_t coderItStatus=0;                    // coder decoding status
bool coderClock=0;                          // current physical coder clock value
bool coderClock0=0;                         // previous physical coder clock value
bool coderData=0;                           // current physical coder data value
bool coderData0=0;                          // previous physical coder data value
bool coderSwitch=0;                         // current physical coder switch value
#endif // MUXED_CODER

uint16_t coderTimerPoolingInterval=1;       // delay betxeen Its (mS) changed by init
uint8_t coderStrobeNumber=3;                // 1st strobe delay (2nd strobe delay is 1)
volatile int32_t* coderTimerCount=nullptr;  // ptr to current value to be inc or dec


// pico2_pins

uint8_t gpio_clock_pin;
uint8_t gpio_data_pin;
uint8_t gpio_switch_pin;
#ifdef MUXED_CODER
uint8_t gpio_sel0_pin;
uint8_t coder_nb;
uint8_t coder_sel_nb;
uint32_t sel_gpio_mask=0;
#endif  // MUXED_CODER

extern volatile uint32_t millisCounter;

extern PIO pio;


#ifndef MUXED_CODER
bool coderTimerHandler(){

    coderClock=gpio_get(gpio_clock_pin);
    
    if(coderClock == coderClock0){                                // no change 
        if(coderItStatus<coderStrobeNumber){                      // wait for change after strobe delay
            coderItStatus++;return true;}
        if(coderItStatus>coderStrobeNumber){                      // 2nd strobe fail
            coderItStatus=0;return true;}
        return true;
    }
    else{                                                         // clock change detected 
        coderData=gpio_get(gpio_data_pin);                         // latch data

        if(coderItStatus<coderStrobeNumber){                      // change to close to previous valid one : ignore it
            coderItStatus=0;return true;}
                                                            
        if(coderItStatus==coderStrobeNumber){     
            coderItStatus++;return true;}                              // 1st strobe passed wait next It
    
    }
 
    coderClock0=coderClock;                                       // valid clock change detected after 2 strobes delay
    coderItStatus=0;

    if(coderTimerCount!=nullptr){                                 // coder_switch used as speed multiplier

        coderSwitch=gpio_get(gpio_switch_pin);

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

void coderInit(uint8_t ck,uint8_t data,uint8_t sw,uint16_t ctpi,uint8_t cstn){
    
    gpio_clock_pin=ck;
    gpio_data_pin=data;
    gpio_switch_pin=sw;

    coderTimerPoolingInterval=ctpi;
    coderStrobeNumber=cstn;

    coderItStatus=0;

    gpio_init(gpio_data_pin);gpio_set_dir(gpio_data_pin,GPIO_IN); 
    gpio_init(gpio_clock_pin);gpio_set_dir(gpio_clock_pin,GPIO_IN);
    gpio_init(gpio_switch_pin);gpio_set_dir(gpio_switch_pin,GPIO_IN);

    coderClock0=gpio_get(gpio_clock_pin);
    coderData0=gpio_get(gpio_data_pin);

    //while(1){
    printf(" -coder init d:%d c:%d s:%d\n",gpio_get(gpio_data_pin),gpio_get(gpio_clock_pin),gpio_get(gpio_switch_pin));
    //sleep_ms(1000);}
}
#endif // MUXED_CODER



#ifdef MUXED_CODER
bool coderTimerHandler(Coder* c){

    for(uint8_t coder=0;coder<coder_nb;coder++){

        c[coder]->coderClock=gpio_get(pio_clock_pin);
        gpio_put_masked(sel_gpio_mask, coder << gpio_base);         // sel current coder
    
        if(c[coder]->coderClock == c[coder]->coderClock0){                // no change 
            if(c[coder]->coderItStatus<coderStrobeNumber){             // wait for change after strobe delay
                c[coder]->coderItStatus++;continue;}
            if(c[coder]->coderItStatus>coderStrobeNumber){             // 2nd strobe fail
                c[coder]->coderItStatus=0;continue;}
            continue;
        }
        else{                                                       // clock change detected 
            c[coder]->coderData=gpio_get(pio_data_pin);                // latch data

            if(c[coder]->coderItStatus<coderStrobeNumber){             // change to close to previous valid one : ignore it
                c[coder]->coderItStatus=0;continue;}
                                                            
            if(c[coder]->coderItStatus==coderStrobeNumber){     
                c[coder]->coderItStatus++;continue;}                   // 1st strobe passed wait next It
        }
 
        c[coder]->coderClock0=c[coder]->coderClock;                       // valid clock change detected after 2 strobes delay
        c[coder]->coderItStatus=0;

        if(coderTimerCount!=nullptr){

            coderSwitch[coder]=gpio_get(pio_switch_pin);            // coder_switch used as speed multiplier

            if((!coderClock[coder])^coderData[coder]){
                (*(c[coder]->(coderTimerCount+coder)))-=1+c[coder]->coderSwitch;
            } 
            else {
                (*(c[coder]->(coderTimerCount+coder)))+=1+c[coder]->coderSwitch;
            }
        }

    // here accelerator management could be added
    }
    return true;    // relancer le timer
}

void coderInit(Coders* c,uint8_t ck,uint8_t data,uint8_t sw,uint8_t sel0,uint8_t sel_nb,uint8_t nb,uint16_t ctpi,uint8_t cstn){
 
    gpio_clock_pin=ck;
    gpio_data_pin=data;
    gpio_switch_pin=sw;
    gpio_base=sel0;
    coder_sel_nb=sel_nb;
    coder_nb=nb;

    coderTimerPoolingInterval=ctpi;
    coderStrobeNumber=cstn;

    gpio_init(pio_data_pin);gpio_set_dir(gpio_data_pin,GPIO_IN); 
    gpio_init(pio_clock_pin);gpio_set_dir(gpio_clock_pin,GPIO_IN);
    gpio_init(pio_switch_pin);gpio_set_dir(gpio_switch_pin,GPIO_IN);

    for(int pin=gpio_base;pin<pin+coder_sel_nb;pin++){
        sel_gpio_mask |=1u<<pin;
    }
    gpio_set_dir_out_masked(mask);

    for(uint8_t coder=0;coder<coder_nb;coder++){
        gpio_put_masked(sel_gpio_mask,coder<<gpio_base);        // sel one coder
        coderClock0[coder]=gpio_get(pio_clock_pin);             // get clock
        coderData0[coder]=gpio_get(ggpio_data_pin);             // get data
        coderSwitch0[coder]=gpio_get(gpio_switch_pin);          // get switch
        printf(" -coder#%d init d:%d c:%d s:%d\n",coder,coderData0[coder],coderClock0[coder],gpio_get(pio_switch_pin));
        c[coder]->coderItStatus=0; 
        c[coder]->coderClock=0;
        c[coder]->coderClock0=0;
        c[coder]->coderData=0;
    }
}

#endif  // MUXED_CODER

void coderSetup(volatile int32_t* cTC){
    coderTimerCount=cTC;}
