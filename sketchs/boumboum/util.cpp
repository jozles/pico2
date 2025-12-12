#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "bb_i2s.h"
#include "const.h"
#include "util.h"
#include "coder.h"
#include "frequences.h"

//PIO pio = pio0;
//uint offset;
//uint sm;

extern volatile uint32_t millisCounter;

volatile uint32_t durOffOn[]={LEDOFFDUR,LEDONDUR};
volatile bool led=false;
volatile uint32_t ledBlinker=0;

struct repeating_timer millisTimer;

bool millisTimerHandler(struct repeating_timer *t){
    millisCounter++;
    coderTimerHandler();
    return true;
}


void setup(){

    gpio_init(LED);gpio_set_dir(LED,GPIO_OUT); gpio_put(LED,LOW);
    //gpio_init(LED_BLUE);gpio_set_dir(LED_BLUE,GPIO_OUT);gpio_put(LED_BLUE,LOW);
    //gpio_init(LED_RED);gpio_set_dir(LED_RED,GPIO_OUT);gpio_put(LED_RED,LOW);
    //gpio_set_dir(LED_GREEN,GPIO_OUT);gpio_put(LED_GREEN,LOW);

    gpio_init(PIN_DCDC_PSM_CTRL);gpio_set_dir(PIN_DCDC_PSM_CTRL, GPIO_OUT);
    gpio_put(PIN_DCDC_PSM_CTRL, 1); // PWM mode for less Audio noise
    
    // irq timer
    add_repeating_timer_ms(1, millisTimerHandler, NULL, &millisTimer);

    fillSineWaveForms();
    bb_i2s_start();

}

void dumpVal(uint32_t val){
    
    for(int i=0;i<4;i++){
        uint8_t v0=val>>(i*8)&0xff;
        printf("%02x",v0);
    }
    printf("\n");
}

void dumpStr16(int32_t* str){
    printf("%p    ",str);
    for(uint32_t i=0;i<16;i++){
        printf("%08x ",str[i]);
    }
    for(uint32_t i=0;i<16;i++){
        for(uint8_t j=0;j<4;j++){
            uint8_t v0=(str[i]>>((3-j)*8))&0x000000FF;
            if(v0>=0x20 && v0<0x7f){printf("%c",v0);}
            else{printf(".");}
        }
        printf(" ");
    }
    printf("\n");
}

void dumpStr(int32_t* str,uint32_t nb){
    for(uint32_t i=0;i<nb;i+=16){
        dumpStr16(&str[i]);
    }
    printf("\n");
}