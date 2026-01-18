#include <stdio.h>
#include "pico/stdlib.h"
#include "const.h"
#include "coder.h"
#include "util.h"
#include "hardware/pio.h"

#include "test.h"
#include "frequences.h"
#include "leds.h"
#include "st7789.h"

// leds

extern volatile uint32_t durOffOn[];
extern volatile bool led;
extern volatile uint32_t ledBlinker;

// millis

volatile uint32_t millisCounter=0;

// coder    

volatile int32_t coder1Counter=0;
volatile int32_t coder1Counter0=0;

// frequence

volatile int16_t freq_lin=0;
volatile int16_t amplitude=0;   

struct voice voices[VOICES_NB];

// ws2812

static PIO pioWs = ws2812_pio;   // pio0 used by i2s


int main() {
    stdio_init_all();
    sleep_ms(10000);printf("\n+boumboum \n");

    gpio_init(LED);gpio_set_dir(LED,GPIO_OUT); gpio_put(LED,LOW);
    
    setup();

    int smWs0=ledsWs2812Setup(pioWs,WS2812_LED_PIN_0);  // max 4 !

    st7789_setup(40000000);

    voices[0].basicWaveAmpl[WAVE_SINUS]=MAX_AMP_VAL;
    voices[0].genAmpl=6000;
    freq_lin=1943;      // 440Hz

    coder1Counter=freq_lin;
    voices[0].frequency=calcFreq(coder1Counter);

            //tft_fill_rect(0,0,TFT_H,TFT_W, 0xFFFF);
            //tft_fill_rect(50, 50, 140, 140, 0x0000);
            //tft_fill_rect(96,50,4,140, 0xFFFF);
            //tft_draw_text_12x12_block_(50, 100, "ST7789", 0x0000, 0xFFFF,2);

    while(1){
        sleep_ms(500);gpio_put(LED,HIGH);sleep_ms(500);gpio_put(LED,LOW);
        ledsWs2812Test(pioWs,smWs0,700);
        test_st7789();
    }

    while (true) {

        LEDBLINK

        if(coder1Counter!=coder1Counter0){

            coder1Counter0=coder1Counter;
            voices[0].newFrequency=calcFreq(coder1Counter);

            printf("freq:%5.3f ampl:%d   \r",voices[0].frequency,voices[0].genAmpl);
            
            //testSample(coder1Counter,amplitude);
            
        }

        ledsWs2812Test(pioWs,smWs0,700);


    
    }

}
