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

uint16_t voicesFreqCoders[VOICES_NB];
uint16_t voicesMaxFreqCoders[VOICES_NB];
volatile bool voicesSw[CODER_NB];                // coder it handler scans all physical coders

volatile int16_t amplitude=0;
extern uint16_t amplLevel[];

// ws2812

static PIO pioWs = ws2812_pio;   // pio0 used by i2s

// i2s

extern volatile bool i2s_buf_free[];
extern int32_t* i2s_buffer[];

// --------

uint8_t coders_for_ampl(uint8_t currVoice);
uint8_t coders_for_freq();

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

    init_test_7789(20,25*8,0,TFT_H-12*8,TFT_H,1);       // init screen animation

    voiceInit((uint16_t)1943,voices);
    
    i2s_start();                                        // launch sound output

    for(uint8_t v=0;v<VOICES_NB;v++){
        voicesFreqCoders[v]=1944; // 440Hz @1943 _ 1944 force first display
        voicesMaxFreqCoders[v]=voices[v].maxCoderFreq;
        voices[v].coderAmpl[W_SINUS]=25;    // 5793
        voices[v].basicWaveAmpl[W_SINUS]=amplLevel[voices[v].coderAmpl[W_SINUS]];
        voices[v].coderSwF=true;
        voicesSw[v]=voices[v].coderSwF;
        for(uint8_t a=0;a<W_NB;a++){
            voices[v].coderSw[a]=true;
        }        
    }

    while(1){
        currVoice=coders_for_freq();
        printf("currVoice:%d\n",currVoice);delay_ms(1);
        coders_for_ampl(currVoice);
    }
}


// ****** coders for voice[currvoice] ampl ******
uint8_t coders_for_ampl(uint8_t currVoice)
{    
    tft_fill_rect_blank(0,0,TFT_H,TFT_W);
    #define LINE_LEN TFT_W/12+1
    char buf[LINE_LEN];memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
    sprintf(buf,"voice:%d %f amp",currVoice,voices[currVoice].frequency);
    tft_draw_text_12x12_dma_mult(0,0,buf,BLUE,0x0000,1);

    coderSetup(&voices[currVoice].coderAmpl[0],&voices[currVoice].coderSw[0],&voices[currVoice].maxCoderAmpl[0],W_NB);

    #define SWIGNORE 2000
    uint32_t swIgnore=millisCounter;
    bool oneTime=true;
    
    while (1) {

gpio_put(TST_PIN,1);

if(i2s_buf_free[0]){fillVoiceBuffer(i2s_buffer[0],&voices[currVoice],0,0);}
if(i2s_buf_free[1]){fillVoiceBuffer(i2s_buffer[1],&voices[currVoice],0,1);}

gpio_put(TST_PIN,0);

        uint16_t ccAmpl=0;
        
        ws_show_3(30);
        ledblinkn(2);
        test_st7789_2();    // animation balayage de lignes
        debug_ticker();

        for(uint8_t coder=0;coder<W_NB;coder++){

            if((millisCounter-swIgnore)>=SWIGNORE){
                //printf("cSw:%d\n",voices[currVoice].coderSw[coder]);
                if((volatile int)voices[currVoice].coderSw[coder]==0){return coder;}
            }
            
            uint32_t cc=voices[currVoice].coderAmpl[coder];
            
            if(cc!=voices[currVoice].coderAmpl0[coder] || oneTime){
                
                oneTime=false;

                memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
                
                buf[0]=coder+48;
                sprintf(buf+2,"%4d ",cc);       // valeur courante coder
 
                voices[currVoice].coderAmpl0[coder]=cc; 

                if(cc!=voices[currVoice].coderAmpl0[coder]){            // oneTime
                    int16_t ccAmpl=cc;
                    if(ccAmpl>MAX_16B_LINEAR_VALUE-1){ccAmpl=MAX_16B_LINEAR_VALUE-1;}
                    if(ccAmpl<MIN_16B_LINEAR_VALUE){ccAmpl=MIN_16B_LINEAR_VALUE;}
                }
                voices[currVoice].basicWaveAmpl[coder]=amplLevel[ccAmpl];           // ampl value for coder value 
                        
                sprintf(buf+7,"%5d",voices[currVoice].basicWaveAmpl[coder]);   
        
                tft_draw_text_12x12_dma_mult(0,coder*(12*2+1)+27,buf,GREEN,0x0000,1);  //07ef

                printf("coder:%d cc:%d sw:%d ampl:%d waveAmpl:%d b:%s\n",
                    coder,cc,voices[currVoice].coderSw[coder],voices[currVoice].coderAmpl[coder],voices[currVoice].basicWaveAmpl[coder],buf);       
            }
        }   
    }
}

// ****** coders for voice[].freq ******
uint8_t coders_for_freq()
{
    #define SWIGNORE 2000
    uint32_t swIgnore=millisCounter;
    bool oneTime=true;

    tft_fill_rect_blank(0,0,TFT_H,TFT_W);
    #define LINE_LEN TFT_W/12+1
    char buf[LINE_LEN];memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
    sprintf(buf,"voices frequencies ",currVoice);
    tft_draw_text_12x12_dma_mult(0,0,buf,BLUE,0x0000,1);

    coderSetup(voicesFreqCoders,voicesSw,voicesMaxFreqCoders,VOICES_NB);

    while (1) {

gpio_put(TST_PIN,1);

if(i2s_buf_free[0]){fillVoiceBuffer(i2s_buffer[0],&voices[currVoice],0,0);}
if(i2s_buf_free[1]){fillVoiceBuffer(i2s_buffer[1],&voices[currVoice],0,1);}

gpio_put(TST_PIN,0);

        uint16_t ccFreq=0;
        
        ws_show_3(30);
        ledblinkn(2);
        test_st7789_2();    // animation balayage de lignes
        debug_ticker();

        //if((millisCounter-probeBlinker)>1000){probeBlinker=millisCounter;printf("%d\n",probe);}  // test existence coderTimerHandler()

        for(uint8_t coder=0;coder<VOICES_NB;coder++){

            if((millisCounter-swIgnore)>=SWIGNORE){ 
                bool vs=voicesSw[coder];          
                voices[coder].coderSwF=vs;
                //printf("vs:%d\n",vs);
                if((volatile int)vs==0){return coder;}
            }
            
            uint32_t cc=voicesFreqCoders[coder];
            
            if(cc!=voices[coder].coderFreq || oneTime){

                oneTime=false;

                #define LINE_LEN TFT_W/12+1
                char buf[LINE_LEN];memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
                
                buf[0]=coder+48;
                sprintf(buf+2,"%4d ",cc);       // valeur courante coder
 
                if(cc!=voices[coder].coderFreq){            // oneTime
                    voices[coder].coderFreq=cc;
                    float f=calcFreq(cc);
                    setNewFrequency(f,&voices[coder]);
                }              
                sprintf(buf+7,"%4.2f  ",voices[coder].newFrequency); // valeur fréquence pour valeur codeur*/     
                
                tft_draw_text_12x12_dma_mult(0,coder*(12*2+1)+27,buf,0x07EF,0x0000,1);

                printf("coder:%d cc:%d sw:%d coderFreq:%d frequency:%f b:%s\n",
                    coder,cc,voices[coder].coderSwF,voices[coder].coderFreq,voices[coder].frequency,buf);                      
            }
        }    
    }
}
#endif  // BB_TEST_MODE
