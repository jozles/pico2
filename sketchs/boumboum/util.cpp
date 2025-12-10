#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "bb_i2s.h"
#include "const.h"
#include "util.h"
#include "coder.h"

//PIO pio = pio0;
//uint offset;
//uint sm;

extern volatile uint32_t millisCounter;

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

    bb_i2s_start();

}