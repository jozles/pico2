#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "const.h"
#include "coder.h"
#include "util.h"
#include "menus.h"
#include "st7789.h"
#include "leds.h"
#include "frequences.h"

volatile uint32_t millisCounter=0;

Voice voices[VOICES_NB];

Lfo lfos[LFOS_NB];

// frequence/ampl

volatile int16_t voicesFreqCoders[VOICES_NB];
uint16_t voicesMaxFreqCoders[VOICES_NB];
volatile int16_t voicesAmplCoders[VOICES_NB];
uint16_t voicesMaxAmplCoders[VOICES_NB];
volatile bool voicesSw[CODER_NB];                // coder it handler scans all physical coders
volatile int16_t voicesWaveAmplCoders[W_NB];
uint16_t voicesMaxWaveAmplCoders[W_NB];
extern uint16_t amplLevel[];
volatile int16_t lfosFreqCoders[LFOS_NB];
uint16_t lfosMaxFreqCoders[LFOS_NB];

extern float lfosFrequency[LFOS_NB];             // current lfo freq
extern int16_t coderLfos[LFOS_NB]; 

// i2s

extern volatile bool i2s_buf_free[];
extern int32_t* i2s_buffer[];
int32_t* i2s_buf=nullptr;           // last loaded buffer for scope

// loop

#define SWIGNORE 1000
uint32_t swIgnore=millisCounter;

// fill voices buffers
void fillVoices()
{
    gpio_put(TST_PIN,1);

    if(i2s_buf_free[0]){fillVoiceBuffer(i2s_buffer[0],&voices[0],0,0);i2s_buf=i2s_buffer[0];}
    if(i2s_buf_free[1]){fillVoiceBuffer(i2s_buffer[1],&voices[0],0,1);i2s_buf=i2s_buffer[1];}

    gpio_put(TST_PIN,0);
}

// ****** inits ******
void menus_init(){    
    for(uint8_t v=0;v<VOICES_NB;v++){
        voicesFreqCoders[v]=voices[v].coderFreq; // 440Hz @1943 _ 1944 force first display
        voicesMaxFreqCoders[v]=voices[v].maxCoderFreq;
        voicesAmplCoders[v]=voices[v].coderGenAmpl;
        voicesMaxAmplCoders[v]=voices[v].maxCoderGenAmpl;
        voices[v].coderAmpl[W_SINUS]=25;    // 5793
        voices[v].basicWaveAmpl[W_SINUS]=amplLevel[voices[v].coderAmpl[W_SINUS]];
        voices[v].coderSwF=true;
        
        for(uint8_t a=0;a<W_NB;a++){
            voices[v].coderSw[a]=true;
            voicesMaxWaveAmplCoders[a]=voices[v].maxCoderAmpl[a];   // voicesMaxWaveAmplCoders[W_NB] est un tableu pour le coderHandler
        }        
    }
    for(uint8_t c=0;c<CODER_NB;c++){
        voicesSw[c]=1;
    }
}

// ****** coders for voice[currvoice] ampl ******
uint8_t coders_for_wavesAmpl(uint8_t currVoice)
{ 
    bool mode_scope=false;
    uint16_t begline=27;                  

    // display title
    tft_fill_rect_blank(0,0,TFT_H,TFT_W);
    #define LINE_LEN TFT_W/12+1
    char buf[LINE_LEN];memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
    sprintf(buf,"v:%d %4.3f amp",currVoice,voices[currVoice].frequency);
    tft_draw_text_12x12_dma_mult(0,0,buf,0x001f,0x0000,1);
    volatile bool firstDisplay=true;

    for(uint8_t a=0;a<W_NB;a++){voicesWaveAmplCoders[a]=voices[currVoice].coderAmpl[a];}
    coderSetup(voicesWaveAmplCoders,voicesSw,voicesMaxWaveAmplCoders,W_NB);    
    
    while (1) {

        fillVoices();
        
        ws_show_3(30);
        ledblinkn(2);
        if(!mode_scope){test_st7789_2();}       // animation balayage de lignes
        debug_ticker();

        for(uint8_t coder=0;coder<W_NB;coder++){

            // gestion switchs
            if((millisCounter-swIgnore)>=SWIGNORE){     // debounce
                volatile int vs=voicesSw[coder];          
                voices[coder].coderSwF=vs;
                if(vs==0){
                    swIgnore=millisCounter;
                    if(coder==W_NB-1){          // return             
                        voicesSw[coder]=1;
                        return coder;}
                    else {
                        mode_scope=!mode_scope;
                        tft_fill_rect_blank(begline,0,TFT_H-begline,TFT_W);
                    }
                }
            }
         
            // gestions coders
            int32_t cc=voicesWaveAmplCoders[coder];                         // cc actual coder value

            int16_t* cp=&voices[currVoice].coderAmpl[coder];            
            
            if(cc!=*cp || firstDisplay){     // coder change or first display

                memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;

                if(cc!=*cp){                                                // if coder change only (not for first display)
                    if(cc>MAX_16B_LINEAR_VALUE-1){cc=MAX_16B_LINEAR_VALUE-1;}
                    else if(cc<MIN_16B_LINEAR_VALUE){cc=MIN_16B_LINEAR_VALUE;}
                    *cp=cc;
                    voices[currVoice].basicWaveAmpl[coder]=amplLevel[cc];   // update ampl value for coder value 
                }

                // display
                if(!mode_scope){
                    buf[0]=coder+48;
                    sprintf(buf+2,"%2d  %5d",cc,voices[currVoice].basicWaveAmpl[coder]);          
                    tft_draw_text_12x12_dma_mult(0,coder*(12*2+1)+begline,buf,GREEN,0x0000,1);
                }
                printf("coder:%d cc:%d sw:%d ampl:%d waveAmpl:%d b:%s\n",
                    coder,cc,voices[currVoice].coderSw[coder],voices[currVoice].coderAmpl[coder],voices[currVoice].basicWaveAmpl[coder],buf);       
            }
        }
        firstDisplay=false;
        if(mode_scope && i2s_buf!=nullptr){scope(i2s_buf,SAMPLES_PER_BUFFER,voices[currVoice].frequency,begline,false);}   
    }
}

// ****** coders for voice[].freq ******
uint8_t coders_for_freq(uint8_t currVoice)
{
    // display title
    tft_fill_rect_blank(0,0,TFT_H,TFT_W);
    #define LINE_LEN TFT_W/12+1
    char buf[LINE_LEN];memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
    sprintf(buf,"voices ");
    tft_draw_text_12x12_dma_mult(0,0,buf,BLUE,0x0000,1);
    volatile bool firstDisplay=true;    

    coderSetup(voicesFreqCoders,voicesSw,voicesMaxFreqCoders,VOICES_NB);

    while (1) {

        fillVoices();
        
        ws_show_3(30);
        ledblinkn(2);
        test_st7789_2();    // animation balayage de lignes
        debug_ticker();

        for(uint8_t coder=0;coder<VOICES_NB;coder++){

            // gestion switchs
            if((millisCounter-swIgnore)>=SWIGNORE){ 
                volatile int vs=voicesSw[coder];          
                voices[coder].coderSwF=vs;
                if((volatile int)vs==0){
                    swIgnore=millisCounter;
                    voicesSw[coder]=1;
                    return coder;}
            }
            
            // gestions coders
            float ccFreq=voices[coder].frequency;               // ccFreq prev freq value for voice[coder] (for display)
            uint32_t cc=voicesFreqCoders[coder];                // cc     actual coder value
            int16_t* cp=&voices[coder].coderFreq;
            
            if(cc!=voices[coder].coderFreq || firstDisplay){

                #define LINE_LEN TFT_W/12+1
                char buf[LINE_LEN];memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
                 
                if(cc!=voices[coder].coderFreq){                // if coder change only (not for first display)
                    voices[coder].coderFreq=cc;
                    float f=calcFreq(cc);
                    setVoiceFrequency(f,&voices[coder]);          // update freq value for coder value
                    ccFreq=voices[coder].frequency;          
                }

                // display
                buf[0]=coder+48;                
                sprintf(buf+2,"%4d  %4.2f     ",cc,ccFreq);     // actual freq value                
                tft_draw_text_12x12_dma_mult(0,coder*(12*2+1)+27,buf,0x07EF,0x0000,1);

                printf("coder:%d cc:%d sw:%d coderFreq:%d frequency:%f b:%s\n",
                    coder,cc,voices[coder].coderSwF,voices[coder].coderFreq,voices[coder].frequency,buf);                      
            }
        }
        firstDisplay=false;            
    }
}

uint8_t coders_for_genAmpl(uint8_t currVoice)
{
    // display title
    tft_fill_rect_blank(0,0,TFT_H,TFT_W);
    #define LINE_LEN TFT_W/12+1
    char buf[LINE_LEN];memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
    sprintf(buf,"Voices Amplifier ");
    tft_draw_text_12x12_dma_mult(0,0,buf,BLUE,0x0000,1);
    volatile bool firstDisplay=true;    

    coderSetup(voicesAmplCoders,voicesSw,voicesMaxAmplCoders,VOICES_NB);

    while (1) {

        fillVoices();
        
        ws_show_3(30);
        ledblinkn(2);
        test_st7789_2();    // animation balayage de lignes
        debug_ticker();

        for(uint8_t coder=0;coder<VOICES_NB;coder++){

            // gestion switchs
            if((millisCounter-swIgnore)>=SWIGNORE){ 
                volatile int vs=voicesSw[coder];          
                voices[coder].coderSwF=vs;
                if((volatile int)vs==0){
                    swIgnore=millisCounter;
                    voicesSw[coder]=1;
                    return coder;}
            }
            
            // gestions coders
            uint16_t ccAmpl=voices[coder].genAmpl;                      // ccAmpl prev genAmpl value for voice[coder] (for display)
            int32_t cc=voicesAmplCoders[coder];
            
            if(cc!=voices[coder].coderGenAmpl || firstDisplay){

                #define LINE_LEN TFT_W/12+1
                char buf[LINE_LEN];memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
 
                if(cc!=voices[coder].coderGenAmpl){                     // if coder change only (not for first display)
                    if(cc>MAX_16B_LINEAR_VALUE-1){cc=MAX_16B_LINEAR_VALUE-1;}
                    else if(cc<MIN_16B_LINEAR_VALUE){cc=MIN_16B_LINEAR_VALUE;}
                    voicesAmplCoders[coder]=cc;
                    voices[coder].coderGenAmpl=cc;
                    ccAmpl=amplLevel[cc];
                    voices[coder].genAmpl=ccAmpl;                       // update ampl value for coder value
                }

                // display
                buf[0]=coder+48;                
                sprintf(buf+2,"%2d  %d      ",cc,ccAmpl);               // actual ampl value                   
                tft_draw_text_12x12_dma_mult(0,coder*(12*2+1)+27,buf,0x07EF,0x0000,1);

                printf("coder:%d cc:%d sw:%d coderGenAmpl:%d genAmpl:%d b:%s\n",
                    coder,cc,voices[coder].coderSwF,voices[coder].coderGenAmpl,voices[coder].genAmpl,buf); 
            }
        }
        firstDisplay=false;    
    }
}    

// ****** coders for voice[].freq ******
uint8_t coders_for_lfos_freq(uint8_t currVoice)
{
    // display title
    tft_fill_rect_blank(0,0,TFT_H,TFT_W);
    #define LINE_LEN TFT_W/12+1
    char buf[LINE_LEN];memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
    sprintf(buf,"lfos ");
    tft_draw_text_12x12_dma_mult(0,0,buf,BLUE,0x0000,1);
    volatile bool firstDisplay=true;    

    coderSetup(lfosFreqCoders,voicesSw,lfosMaxFreqCoders,VOICES_NB);

    while (1) {

        fillVoices();
        
        ws_show_3(30);
        ledblinkn(2);
        test_st7789_2();    // animation balayage de lignes
        debug_ticker();

        for(uint8_t coder=0;coder<LFOS_NB;coder++){

            // gestion switchs
            if((millisCounter-swIgnore)>=SWIGNORE){ 
                volatile int vs=voicesSw[coder];          
                voices[coder].coderSwF=vs;
                if((volatile int)vs==0){
                    swIgnore=millisCounter;
                    voicesSw[coder]=1;
                    return coder;}
            }
            
            // gestions coders
            float ccLfos=lfosFrequency[coder];           // ccFreq prev freq value for voice[coder] (for display)
            uint32_t cc=lfosFreqCoders[coder];                // cc     actual coder value
            //int16_t* cp=&coderLfos[coder];
            
            if(cc!=coderLfos[coder] || firstDisplay){

                #define LINE_LEN TFT_W/12+1
                char buf[LINE_LEN];memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
                 
                if(cc!=coderLfos[coder]){                // if coder change only (not for first display)
                    coderLfos[coder]=cc;
                    float f=calcFreq(cc)/10000;
                    setLfosFrequency(f,coder);          // update freq value for coder value
                    ccLfos=lfosFrequency[coder];          
                }

                // display
                buf[0]=coder+48;                
                sprintf(buf+2,"%4d  %4.2f     ",cc,ccLfos);     // actual freq value                
                tft_draw_text_12x12_dma_mult(0,coder*(12*2+1)+27,buf,0x07EF,0x0000,1);

                printf("coder:%d cc:%d sw:%d coderLfos:%d lfoFreq:%f b:%s\n",
                    coder,cc,voices[coder].coderSwF,coderLfos[coder],lfosFrequency[coder],buf);                      
            }
        }
        firstDisplay=false;            
    }
}
