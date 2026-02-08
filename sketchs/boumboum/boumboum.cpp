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

volatile int16_t coderCounter[CODER_NB];
volatile int16_t coderCounter0[CODER_NB];

#ifdef MUXED_CODER

extern Coders c[];

Coders ct[CODER_NB];                            // cinematic

uint8_t currFonc=0;
uint32_t* currCoderBank=nullptr;
uint32_t* currCoderBank0=nullptr;
#endif // MUXED_CODER

// voices 

uint8_t currVoice=0;
Voice voices[VOICES_NB];

// frequence

volatile int16_t amplitude=0;   

// ws2812

static PIO pioWs = ws2812_pio;   // pio0 used by i2s

// --------

int main() {
    stdio_init_all();
    sleep_ms(10000);printf("\n+boumboum \n");
    

//gpio_init(13);gpio_set_dir(13,GPIO_OUT); gpio_put(13,1);
//while(1){gpio_put(13,0);sleep_ms(1);gpio_put(13,1);sleep_ms(1000);}

    setup();

#ifdef BB_TEST_MODE

    coderSetup(coderCounter);

    for(uint8_t f=0;f<CODER_NB;f++){coderCounter[f]=0;coderCounter0[f]=coderCounter[f]+1;} 

    voices[0].basicWaveAmpl[WAVE_SINUS]=MAX_AMP_VAL;
    voices[0].genAmpl=6000;
    voices[0].frequencyCc=1943;      // 440Hz
    voices[0].frequency=calcFreq(voices[0].frequencyCc);
    voices[0].newFrequency=voices[0].frequency;
   
#ifndef MUXED_CODER

    init_test_7789(1000,32,0,TFT_W,TFT_H,2);

    *coderCounter=voices[0].frequencyCc;

    while (1) {

        ws_show_3(30);

        LEDBLINK

        test_st7789();

        if(*coderCounter!=*coderCounter0){

            *coderCounter0=*coderCounter;
            voices[0].frequencyCc=*coderCounter;
            voices[0].newFrequency=calcFreq(voices[0].frequencyCc);

            tft_draw_int_12x12_dma_mult(0,12,0xffff,0x0000,1,voices[0].frequencyCc);

            tft_draw_text_12x12_dma_mult(50,12,"->", 0xFFFF, 0x0000,1);
            tft_draw_float_12x12_dma_mult(80,12,0xffff,0x0000,2,voices[0].newFrequency);
            
            printf("freq:%5.3f ampl:%d   \r",voices[0].frequency,voices[0].genAmpl);           
        }
    }
        #endif  // MUXED_CODER


#ifdef MUXED_CODER

    init_test_7789(20,25*8,0,TFT_H-12*8,TFT_H,1);

    while (1) {

        ws_show_3(30);

        LEDBLINK

        test_st7789_2();

        for(uint8_t coder=0;coder<CODER_NB;coder++){
            
            uint8_t mul=2;
            char s[]={(char)(48+c[coder].coderSwitch),(char)(48+c[coder].coderClock),(char)(48+c[coder].coderData),0x00};
            tft_draw_text_12x12_dma_mult(160,coder*(12*mul+1),s,0xffff,0x0000,mul);            

            uint32_t cc=*(coderCounter+coder);
            
            if(cc!=*(coderCounter0+coder)){

                *(coderCounter0+coder)=cc;
                voices[0].newFrequency=calcFreq(cc);

                tft_draw_int_12x12_dma_mult(0,coder*(12*mul+1),0xFFFF, 0x0000,mul,coder);
                tft_draw_int_12x12_dma_mult(74,coder*(12*mul+1),0xffff,0x0000,mul,cc);
            
                printf("freq:%5.3f ampl:%d   \r",voices[0].frequency,voices[0].genAmpl);           
            }
        }
    }
#endif  // MUXED_CODER


#endif  // BB_TEST_MODE

#ifndef BB_TEST_MODE

        // inits 

        for(uint8_t f=0;f<CODER_NB,f++){
            coderCounter[VOICES_MIXER*CODER_NB+f]=MAX_16B_LINEAR_VALUE/CODER_NB;
            coderCounter[VOICE0_SPECTRUM_MIXER*CODER_NB+f]=MAX_16B_LINEAR_VALUE/CODER_NB;
            coderCounter[VOICE0_ADSR*CODER_NB+f]=MAX_16B_LINEAR_VALUE/CODER_NB;
        }

        currFonc=VOICE0_SPECTRUM_MIXER;
        currVoice=0;

        while(1){
            tft_show(currFonc,currVoice);

            currCoderBank=&coderCounter[currFonc*CODER_NB];
            currCoderBank0=&coderCounter0[currFonc*CODER_NB];

            coderSetup(currCoderBank);
            
            if(currFonc==VOICES_MIXER){autoMixer(currCoderBank,currCoderBank0);}
            if(currFonc<VOICE0_SPECTRUM_MIXER+VOICES_NB){autoMixer(currCoderBank,currCoderBank0);}
            if(currFonc<VOICE0_ADSR+VOICES_NB){adsr(currCoderBank,currCoderBank0);}
        }

#endif  // BB_TEST_MODE

}
