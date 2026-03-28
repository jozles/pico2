#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "const.h"
#include "coder.h"
#include "util.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"


#include "bb_i2s.h"
#include "test.h"
#include "frequences.h"
#include "leds.h"
#include "st7789.h"

// debug

volatile uint32_t int_counter=0;
volatile bool one_time=false;

// leds

extern volatile uint32_t durOffOn[];
extern volatile bool led;
extern volatile uint32_t ledBlinker;

// millis

volatile uint32_t millisCounter=0;
volatile uint32_t probe=0;      // pour debouncer
uint32_t probeBlinker=0;

// coder 

//volatile int16_t coderCounter[CODER_NB];
//volatile int16_t coderCounter0[CODER_NB];
//volatile bool coderSwitchs[CODER_NB];
extern int8_t cOT[];

#ifdef MUXED_CODER

extern Coders c[];

Coders ct[CODER_NB];                            // cinematic

uint8_t currFonc=0;
uint32_t* currCoderBank=nullptr;
uint32_t* currCoderBank0=nullptr;
#endif // MUXED_CODER

// voices 

extern int32_t i2s_buf0[];
extern int32_t i2s_buf1[];

uint8_t currVoice=0;
Voice voices[VOICES_NB];

// frequence/ampl

volatile int16_t amplitude=0;
extern uint16_t amplLevel[];

// ws2812

static PIO pioWs = ws2812_pio;   // pio0 used by i2s

// --------

int main() {

    stdio_init_all();

    sleep_ms(1000);

    gpio_init(TST_PIN);gpio_set_dir(TST_PIN,GPIO_OUT); gpio_put(TST_PIN,LOW);    
    gpio_init(LED);gpio_set_dir(LED,GPIO_OUT); gpio_put(LED,LOW);
    delayBlk(3);        
    printf("\n+boumboum= \n");
    
if (watchdog_caused_reboot()) {
    printf("RESET = WATCHDOG\n");
} else {
    printf("RESET = NORMAL\n");
}

    setup();

#ifdef BB_TEST_MODE

    init_test_7789(20,25*8,0,TFT_H-12*8,TFT_H,1);               // init animation

    i2s_start();

// ****** voice[0] ; freq=440Hz/1953 ; ampl all 0 except sin 6000 ; genAmpl=max ;

    voices[currVoice].genAmpl=0x7fff;
    voices[currVoice].frequencyCc=1943;      
    float f=calcFreq(1943);             // 440Hz
    setNewFrequency(f,&voices[0]);    
    voices[currVoice].frequency=calcFreq(voices[0].frequencyCc);
    voices[currVoice].newFrequency=voices[0].frequency;

    for(uint8_t i=0;i<W_NB;i++){
        voices[currVoice].coderAmpl[i]=0;
        voices[currVoice].coderAmpl0[i]=99;
        voices[currVoice].basicWaveAmpl[i]=0;
    }
    voices[currVoice].coderAmpl[W_SINUS]=20;        // 6000?
    voices[currVoice].basicWaveAmpl[W_SINUS]=amplLevel[voices[currVoice].coderAmpl[W_SINUS]];

    // ****** coders for voice[currvoice] ampl ******
    
    coderSetup(&voices[currVoice].coderAmpl[0],&voices[currVoice].coderSw[0]);

    while (1) {

        uint16_t ccAmpl=0;
        
        ws_show_3(30);
        ledblinkn(2);
        test_st7789_2();    // animation balayage de lignes
        debug_ticker();

        //if((millisCounter-probeBlinker)>1000){probeBlinker=millisCounter;printf("%d\n",probe);}  // test existence coderTimerHandler()

        for(uint8_t cod=0;cod<W_NB;cod++){

            uint8_t coder=cOT[cod];             // ordre physique des coders
            
            uint32_t cc=voices[currVoice].coderAmpl[cod];
            
            if(cc!=voices[currVoice].coderAmpl0[cod]){

                #define LINE_LEN TFT_W/12+1
                char buf[LINE_LEN];memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;

                buf[0]=cod+48;
                sprintf(buf+2,"%4d ",cc);       // valeur courante coder
 
                voices[currVoice].coderAmpl0[cod]=cc;
                /*float f=calcFreq(cc);
                setNewFrequency(f,&voices[cod]);               
                sprintf(buf+7,"%4.2f  ",voices[cod].newFrequency); // valeur fréquence pour valeur codeur*/     

                int16_t ccAmpl=cc;
                if(ccAmpl>MAX_16B_LINEAR_VALUE-1){ccAmpl=MAX_16B_LINEAR_VALUE-1;}
                if(ccAmpl<MIN_16B_LINEAR_VALUE){ccAmpl=MIN_16B_LINEAR_VALUE;}
                voices[currVoice].basicWaveAmpl[cod]=amplLevel[ccAmpl];           // ampl value for coder value         
                sprintf(buf+14,"%5d",voices[currVoice].basicWaveAmpl[cod]);   
        
                tft_draw_text_12x12_dma_mult(0,cod*(12*2+1),buf,0x07EF,0x0000,1);

                printf("coder:%d cc:%d sw:%d ampl:%d  %s\n",
                    cod,cc,voices[currVoice].coderSw[cod],voices[cod].genAmpl,buf);       
            }
        }
    }//*/

#endif  // BB_TEST_MODE

#ifndef BB_TEST_MODE

uint8_t menuLevel=0 ; // 0=frequence des 6 voices
bool init=true;
bool sw=0;

while(1){

    // **** menu ****
    #define VOICESFREQ 0

    switch(menuLevel){

        case VOICESFREQ:
            if(init){
                init==false;
                coderSetup(coderCounter,coderSwitchs);
                for(uint8_t cod=0;cod<VOICES_NB;cod++){
                    uint8_t coder=cOT[cod];
                    coderCounter[coder]=voices[coder].coderFreq;
                }
            }  
            
            for(uint8_t cod=0;cod<VOICES_NB;cod++){    // 1 coder/voice
                uint8_t coder=cOT[cod];
                
                if(coderCounter[coder]!=voices[coder].coderFreq){        // dernière valeur acquise pour le codeur changée ?
                    uint32_t cc=coderCounter[coder];        
                    uint8_t mul=2;
                    tft_fill_rect(coder*(12*mul+1)+2,0,12*mul,TFT_W,0x0000);

                    tft_draw_int_12x12_dma_mult(0,coder*(12*mul+1)+2,0xFFFF, 0x0000,1,coder);     // numéro codeur
                    tft_draw_int_12x12_dma_mult(20,coder*(12*mul+1)+2,0xffff,0x0000,1,cc,4);      // valeur courante codeur

                    voices[coder].coderFreq=cc;
                    voices[coder].newFrequency=calcFreq(cc);    
                    tft_draw_float_12x12_dma_mult(80,coder*(12*mul+1)+5,0xffff,0x0000,1,voices[coder].newFrequency,4);        // freq value for current voice
                }
                if((coderSwitchs[coder]!=voices[coder].coderSw[0])){
                    voices[coder].coderSw[0]=coderSwitchs[coder];
                    if(voices[coder].coderSw[0]!=0){menuLevel++;init=true;sw=coder;}
                }
            }
            break;


        default :break;
    }
}
/*
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
*/
#endif  // BB_TEST_MODE

}
