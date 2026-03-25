#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/pio.h"
#include "const.h"
#include "coder.h"
#include "util.h"
#include "st7789.h"


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
volatile int16_t* coderTimerCount=nullptr;  // ptr to current value to be inc or dec
volatile bool* coderTimerSwitch=nullptr;    // switchs values

uint8_t cOT[CODER_NB]={0,1,2,3,4,7,6,5};    // CODER ORDER TABLE ordre physique

// pico2_pins

uint8_t gpio_clock_pin;
uint8_t gpio_data_pin;
uint8_t gpio_switch_pin;
uint8_t gpio_vcc_pin;
#ifdef MUXED_CODER
uint8_t gpio_sel0_pin;
uint8_t coder_nb;
uint8_t coder_sel_nb;
uint32_t sel_gpio_mask=0;

Coders c[CODER_NB];
#endif  // MUXED_CODER

extern volatile uint32_t millisCounter;
extern volatile uint32_t probe;

extern PIO pio;

extern volatile uint32_t int_counter;
extern volatile bool one_time;


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

void coderInit(uint8_t ck,uint8_t data,uint8_t sw,uint8_t vc,uint16_t ctpi,uint8_t cstn){
    
    gpio_clock_pin=ck;
    gpio_data_pin=data;
    gpio_switch_pin=sw;
    gpio_vcc_pin=vc;

    coderTimerPoolingInterval=ctpi;
    coderStrobeNumber=cstn;

    coderItStatus=0;

    gpio_init(gpio_data_pin);gpio_set_dir(gpio_data_pin,GPIO_IN); 
    gpio_init(gpio_clock_pin);gpio_set_dir(gpio_clock_pin,GPIO_IN);
    gpio_init(gpio_switch_pin);gpio_set_dir(gpio_switch_pin,GPIO_IN);
    gpio_init(gpio_vcc_pin);gpio_set_dir(gpio_vcc_pin,GPIO_OUT);gpio_put(gpio_vcc_pin,1);


    coderClock0=gpio_get(gpio_clock_pin);
    coderData0=gpio_get(gpio_data_pin);

    //while(1){
    printf(" -coder init d:%d c:%d s:%d\n",gpio_get(gpio_data_pin),gpio_get(gpio_clock_pin),gpio_get(gpio_switch_pin));
    //sleep_ms(1000);}
}
#endif // MUXED_CODER

#ifdef MUXED_CODER
bool coderTimerHandler(){
    
    int_counter++;
    if(int_counter>=coderTimerPoolingInterval){
        int_counter=0;

        Coders* cp;
        probe=millisCounter;

        for(uint8_t coder=0;coder<coder_nb;coder++){
            gpio_put_masked(sel_gpio_mask, coder << gpio_sel0_pin);     // sel current coder ; env 6uS le pas de boucle + les traitements

            cp=&c[coder];
            //gpio_put(TST_PIN,1);
            quick_delay(8);         // 9uS semble nécessaire pour stabiliser les coders et 4051 sinon ca fait nimporte quoi
                                    // temps total du step 19uS ! avec 8mS d'intervalle ça semble ok (v1.2)
                                    // mesure 2.5uS total avec le delay !!! incompréhensible ... et ça marche
            //gpio_put(TST_PIN,0);
            // traitement switch (en premier pour ne pas être zappé par les "continue")
            if(cp->coderSwitch!=gpio_get(gpio_switch_pin)){
            if((probe-cp->coderSwitchTime)>CODER_SW_STROBE_MS){
                cp->coderSwitch=!cp->coderSwitch;
                cp->coderSwitchTime=probe;
            }
            if(coderTimerSwitch!=nullptr){
                (*(coderTimerSwitch+coder))=cp->coderSwitch;
            }
            }
        
        
            // détection coder
            cp->coderClock=gpio_get(gpio_clock_pin);                      
            if(cp->coderClock == cp->coderClock0){                 // no change 
                if(cp->coderItStatus<coderStrobeNumber){           // wait for change after strobe delay
                    cp->coderItStatus++;continue;}
                //if(cp->coderItStatus>coderStrobeNumber){           // 2nd strobe fail
                //    cp->coderItStatus=0;continue;}
                continue;
            }
            else{                                                  // clock change detected 
                cp->coderData=gpio_get(gpio_data_pin);             // latch data
                if(cp->coderItStatus<coderStrobeNumber){           // change too close to previous valid one : ignore it
                    cp->coderItStatus=0;continue;}                                                         
                if(cp->coderItStatus==coderStrobeNumber){     
                    cp->coderItStatus++;continue;}                 // 1st strobe passed wait next Int
            }
        
            cp->coderClock0=cp->coderClock;                   // valid clock change detected after 2 strobes delay
            cp->coderItStatus=0;
        
            // traitement coder
            if(coderTimerCount!=nullptr){

                if((!cp->coderClock)^cp->coderData){
                    if(*(coderTimerCount+coder)>0){
                        (*(coderTimerCount+coder))-=1;
                    }
                    else *(coderTimerCount+coder)=0;
                } 
                else {
                    (*(coderTimerCount+coder))+=1; 
                    if(*(coderTimerCount+coder)==0){
                        (*(coderTimerCount+coder))-=1;
                    }  
                }
            }        
        }
    }
    return true;    // relancer le timer
}

void coderInit(uint8_t ck,uint8_t data,uint8_t sw,uint8_t vc,uint8_t sel0,uint8_t sel_nb,uint8_t nb,uint16_t ctpi,uint8_t cstn){

    // ********************* doit absolument etre fait avant la mise en route du timer *******************
    coderTimerCount=nullptr;
    coderTimerSwitch=nullptr;

    gpio_clock_pin=ck;
    gpio_data_pin=data;
    gpio_switch_pin=sw;
    gpio_vcc_pin=vc;
    gpio_sel0_pin=sel0;
    coder_sel_nb=sel_nb;
    coder_nb=nb;

    coderTimerPoolingInterval=ctpi;
    coderStrobeNumber=cstn;

    gpio_init(gpio_data_pin);gpio_set_dir(gpio_data_pin,GPIO_IN); 
    gpio_init(gpio_clock_pin);gpio_set_dir(gpio_clock_pin,GPIO_IN);
    gpio_init(gpio_switch_pin);gpio_set_dir(gpio_switch_pin,GPIO_IN);
    gpio_init(gpio_vcc_pin);gpio_set_dir(gpio_vcc_pin,GPIO_OUT);gpio_put(gpio_vcc_pin,1);

    sleep_ms(10);

    sel_gpio_mask=0;
    for(int pin=gpio_sel0_pin;pin<gpio_sel0_pin+coder_sel_nb;pin++){
        gpio_init(pin);
        gpio_set_function(pin, GPIO_FUNC_SIO);
        sel_gpio_mask |=1u<<pin;
    }
    gpio_set_dir_out_masked(sel_gpio_mask);

    printf("coders sel gpio mask:%X sel0 pin:%d\n sel_nb:%d",sel_gpio_mask,gpio_sel0_pin,coder_sel_nb);

    for(uint8_t coder=0;coder<coder_nb;coder++){
        gpio_put_masked(sel_gpio_mask,coder<<gpio_sel0_pin);     // sel one coder
        sleep_us(10);
        c[coder].coderClock0=gpio_get(gpio_clock_pin);           // get clock
        c[coder].coderData0=gpio_get(gpio_data_pin);             // get data
        c[coder].coderSwitch=0;
        c[coder].coderSwitchTime=0;                              // init debouncer
        printf(" -coder#%d init d:%d c:%d s:%d\n",coder,c[coder].coderData0,c[coder].coderClock0,gpio_get(gpio_switch_pin));
        c[coder].coderItStatus=0; 

/*
        gpio_put(2,0);
        gpio_put(3,0);
        gpio_put(4,0);
        printf("clkpin:%d ",gpio_get(gpio_clock_pin));
        gpio_put(2,1);
        gpio_put(3,0);
        gpio_put(4,0);
        printf("%d ",gpio_get(gpio_clock_pin));
        gpio_put(2,0);
        gpio_put(3,1);
        gpio_put(4,0);
        printf("%d ",gpio_get(gpio_clock_pin)); 
        gpio_put(2,1);
        gpio_put(3,1);
        gpio_put(4,0);
        printf("%d ",gpio_get(gpio_clock_pin));         
        gpio_put(2,0);
        gpio_put(3,0);
        gpio_put(4,1);
        printf("%d ",gpio_get(gpio_clock_pin)); 
        gpio_put(2,1);
        gpio_put(3,0);
        gpio_put(4,1);
        printf("%d ",gpio_get(gpio_clock_pin));
        gpio_put(2,0);
        gpio_put(3,1);
        gpio_put(4,1);
        printf("%d ",gpio_get(gpio_clock_pin));  
        gpio_put(2,1);
        gpio_put(3,1);
        gpio_put(4,1);
        printf("%d\n",gpio_get(gpio_clock_pin)); 
*/                                            
    }
}

uint32_t test_cnt=1;
void slow_coder_test(uint32_t ms){
    
    coderInit(CODER_GPIO_CLOCK,CODER_GPIO_DATA,CODER_GPIO_SW,CODER_GPIO_VCC,CODER_PIO_SEL0,CODER_SEL_NB,CODER_NB,CODER_TIMER_POOLING_INTERVAL_MS,CODER_STROBE_NUMBER);
    for(uint8_t coder=0;coder<coder_nb;coder++){

        //uint8_t selc=coder | 0x08;  // *********************************** force 4051 enable haut ******************** 

        gpio_put_masked(sel_gpio_mask, coder << gpio_sel0_pin);     // sel current coder
        sleep_us(1);
        printf("coder:%d Ck:%d Da:%d Sw:%d\n",coder,gpio_get(CODER_GPIO_CLOCK),gpio_get(CODER_GPIO_DATA),gpio_get(CODER_GPIO_SW));
        sleep_ms(ms);//printf("%d\n",test_cnt++);
    }
}

#endif  // MUXED_CODER

void coderSetup(volatile int16_t* cTC,volatile bool* cTS){
    coderTimerCount=cTC;
    coderTimerSwitch=cTS;
}


