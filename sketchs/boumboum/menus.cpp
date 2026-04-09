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
//#include "mapping.h"

volatile uint32_t millisCounter=0;

// mapping

const char inputs_names[][IN_OUT_NAME_LEN]={
    #define X(name,text) text,
    #include "inputs.def"
    #undef X
};

const char outputs_names[][IN_OUT_NAME_LEN]={
    #define Y(name,text) text,
    #include "outputs.def"
    #undef Y
};

uint8_t  inputs[INPUTS_NB];

void inputsInit(){
    memset(inputs,0x00,INPUTS_NB);
}

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
static int32_t* i2s_buf=nullptr;           // last loaded buffer for scope

// loop

#define SWIGNORE 1000
uint32_t swIgnore=millisCounter;

#define LINE_LEN TFT_W/12+1
char buf[LINE_LEN];

uint16_t begline=27;

const char menu_names[][MENU_NAME_LEN]={
    #define Z(name,text) text,
    #include "menu.def"
    #undef Z
};

/* ----------------------------------------- */

// ******fill voices buffers ******
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

// ****** display title ******
void title_dsp(const char* title,uint8_t currVoice,uint8_t v){
    tft_fill_rect_blank(0,0,TFT_H,TFT_W);
    memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
    switch(v){
        case 0:sprintf(buf,"v:%d %4.3f amp",currVoice,voices[currVoice].frequency);break;
        default:sprintf(buf,"%s ",title);break;
    }        
    tft_draw_text_12x12_dma_mult(0,0,buf,0x001f,0x0000,1);
} 

// ****** switchs
#define SCOPE_MODE -2
int8_t tst_switchs(uint8_t coder,uint8_t maxi){
    if((millisCounter-swIgnore)>=SWIGNORE){ 
        volatile int vs=voicesSw[coder];          
        voices[coder].coderSwF=vs;
        if((volatile int)vs==0){
            swIgnore=millisCounter;
            voicesSw[coder]=1;
            if(coder<maxi){                      
                return coder;}
            return -(coder-maxi+2);     // if maxi == 2 values are -2,-3,-4,-5
        }                               // if maxi == 4 values are -2,-3
    }
    return -1;                          // nothing
} 

// ****** coders for voice[currvoice] ampl ******
uint8_t coders_for_wavesAmpl(uint8_t currVoice)
{ 
    bool mode_scope=false;
    bool firstScope=false;            

    volatile bool firstDisplay=true;

    title_dsp("",currVoice,0);

    for(uint8_t a=0;a<W_NB;a++){voicesWaveAmplCoders[a]=voices[currVoice].coderAmpl[a];}
    coderSetup(voicesWaveAmplCoders,voicesSw,voicesMaxWaveAmplCoders,W_NB);    
    
    while (1) {

        fillVoices();
        
        ws_show_3(30);
        ledblinkn(2);
        if(!mode_scope){test_st7789_2();}       // animation balayage de lignes
        //debug_ticker();

        for(uint8_t coder=0;coder<W_NB;coder++){

            int8_t s=tst_switchs(coder,2);  // coder0 return ; other->scope
            if(s!=-1){printf("s:%d\n",s);}
            if(s>=0){return s;}
            else if(s==SCOPE_MODE){mode_scope=!mode_scope;firstScope=true;}

            // gestions coders
            volatile int32_t cc=voicesWaveAmplCoders[coder];                         // cc actual coder value

            volatile int16_t* cp=&voices[currVoice].coderAmpl[coder];            
            
            if(cc!=*cp || firstDisplay){     // coder change or first display

                memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;

                if(cc!=*cp){                                                // if coder change only (not for first display)
                    //if(cc>MAX_16B_LINEAR_VALUE-1){cc=MAX_16B_LINEAR_VALUE-1;}
                    //else if(cc<MIN_16B_LINEAR_VALUE){cc=MIN_16B_LINEAR_VALUE;}
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
        //printf("%d %d\n",mode_scope,firstScope);
        firstDisplay=false;
        if(mode_scope && i2s_buf!=nullptr){scope(i2s_buf,SAMPLES_PER_BUFFER,voices[currVoice].frequency,begline,false,firstScope,3);firstScope=false;}   
    }
}

// ****** coders for voice[].freq ******
uint8_t coders_for_freq(uint8_t currVoice)
{
    volatile bool firstDisplay=true;

    title_dsp("voices ",currVoice,99);   

    coderSetup(voicesFreqCoders,voicesSw,voicesMaxFreqCoders,VOICES_NB);

    while (1) {

        fillVoices();
        
        ws_show_3(30);
        ledblinkn(2);
        test_st7789_2();    // animation balayage de lignes
        debug_ticker();

        for(uint8_t coder=0;coder<VOICES_NB;coder++){

            int s=tst_switchs(coder,VOICES_NB);
            if(s>=0){return s;}
            
            // gestions coders
            float ccFreq=voices[coder].frequency;               // ccFreq prev freq value for voice[coder] (for display)
            uint32_t cc=voicesFreqCoders[coder];                // cc     actual coder value
            int16_t* cp=&voices[coder].coderFreq;
            
            if(cc!=voices[coder].coderFreq || firstDisplay){

                #define LINE_LEN TFT_W/12+1
                memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
                 
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
    volatile bool firstDisplay=true;    

    title_dsp("Voices Amplifier ",currVoice,99);

    coderSetup(voicesAmplCoders,voicesSw,voicesMaxAmplCoders,VOICES_NB);

    while (1) {

        fillVoices();
        
        ws_show_3(30);
        ledblinkn(2);
        test_st7789_2();    // animation balayage de lignes
        debug_ticker();

        for(uint8_t coder=0;coder<VOICES_NB;coder++){

            int s=tst_switchs(coder,VOICES_NB);            
            if(s>=0){return s;}            
            
            // gestions coders
            uint16_t ccAmpl=voices[coder].genAmpl;                      // ccAmpl prev genAmpl value for voice[coder] (for display)
            int32_t cc=voicesAmplCoders[coder];
            
            if(cc!=voices[coder].coderGenAmpl || firstDisplay){

                #define LINE_LEN TFT_W/12+1
                memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
 
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
    volatile bool firstDisplay=true;
    
    bool mode_scope=false;
    bool firstScope=false;     
    
    title_dsp("lfos ",currVoice,99);

    coderSetup(lfosFreqCoders,voicesSw,lfosMaxFreqCoders,VOICES_NB);

    while (1) {

        fillVoices();
        
        ws_show_3(30);
        ledblinkn(2);
        test_st7789_2();    // animation balayage de lignes
        debug_ticker();

        for(uint8_t coder=0;coder<LFOS_NB;coder++){

            int s=tst_switchs(coder,LFOS_NB);            
            if(s>=0){return s;}
            else if(s==SCOPE_MODE){mode_scope=!mode_scope;firstScope=true;}
            
            // gestions coders
            float ccLfos=lfosFrequency[coder];           // ccFreq prev freq value for voice[coder] (for display)
            uint32_t cc=lfosFreqCoders[coder];                // cc     actual coder value
            //int16_t* cp=&coderLfos[coder];
            
            if(cc!=coderLfos[coder] || firstDisplay){

                #define LINE_LEN TFT_W/12+1
                memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
                 
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
        if(mode_scope && i2s_buf!=nullptr){scope(i2s_buf,SAMPLES_PER_BUFFER,voices[currVoice].frequency,begline,false,firstScope,3);firstScope=false;}             
    }
}

#define MAPPING_CODER_NB 3
volatile int16_t mappingCoders[MAPPING_CODER_NB];  // [0] curr input nb ; [1] curr_input value
uint16_t maxMappingCoders[]={INPUTS_NB,OUTPUTS_NB,0};

#define NB_DSP_LINES 12
#define FIRSTLINEH 20

void mappingDsp(uint8_t inp,uint8_t line,bool rev){
    uint8_t v=convIntToString(buf,inp);
    if(v==1){buf[1]=' ';}
    buf[2]=' ';
    v=convIntToString(buf,inputs[inp]);
    if(v==1){buf[v+3]=' ';}
    buf[v+3+1]=' ';
    memcpy(buf+v+3+1+1,&inputs_names[inputs[inp]],IN_OUT_NAME_LEN);
    buf[v+3+1+1+IN_OUT_NAME_LEN]=' ';
    memcpy(buf+v+3+1+1+IN_OUT_NAME_LEN+1,&inputs_names[inputs[inp]],3); //IN_OUT_NAME_LEN);
    uint16_t fgc=0x07EF;
    uint16_t bgc=0x0000;
    uint16_t buc=fgc;
    if(rev){fgc=bgc;bgc=buc;}
    tft_draw_text_12x12_dma_mult(0,line*((12+2))+FIRSTLINEH,buf,fgc,bgc,1);
}

void fullMappingDsp(uint8_t firstInput,uint8_t currDspInput){
    for(uint8_t l=0;l<NB_DSP_LINES;l++){
        mappingDsp(firstInput+l,l,currDspInput==l);
    }
}

uint8_t coders_for_mapping(){
    
    uint8_t currInput=0;
    uint8_t currDspInput=0;

    coderSetup(mappingCoders,voicesSw,maxMappingCoders,3);

    fullMappingDsp(0,0);

        while(1){

            for(uint8_t coder=0;coder<MAPPING_CODER_NB;coder++){
                int s=tst_switchs(coder,MAPPING_CODER_NB);            
                if(s>=0){return s;}

                uint32_t cc=mappingCoders[coder];
                if(coder==0){
                    if(cc>currInput){                                   // cursor move down
                        if(currDspInput<NB_DSP_LINES){                  // no scroll
                            mappingDsp(currInput,currDspInput,false);   // restore prev
                            mappingDsp(currInput+1,currDspInput+1,true);    
                        }
                    }
                    else if(currInput<INPUTS_NB-1){
                        currDspInput=NB_DSP_LINES-1;
                        fullMappingDsp(currInput++ - NB_DSP_LINES,currDspInput);}    // scroll down
                
                    if(cc<currInput){                                   // cursor move up
                        if(currDspInput>0){                             // no scroll
                            mappingDsp(currInput,currDspInput,false);   // restore prev
                            mappingDsp(currInput-1,currDspInput-1,true);    
                        }
                    }
                    else if(currInput>0){
                        currDspInput=0;
                        fullMappingDsp(currInput--,currDspInput);}       // scroll up
                }
                if(coder==1){
                    inputs[currInput]=cc;
                    mappingDsp(currInput,currDspInput,true);
                }
            }
        }          
}

void lineMenuDsp(uint8_t line,bool rev){
        uint16_t fgc=GREEN;
        uint16_t bgc=0x0000;
        uint16_t buc=fgc;
        if(rev){fgc=bgc;bgc=buc;}
        buf[0]=line+48;
        sprintf(buf,"%2d  %s",line,&menu_names[line][0]);
        //memcpy(buf+3,&menu_names[line][0],MENU_NAME_LEN);       
        tft_draw_text_12x12_dma_mult(0,line*(12*2+1)+begline,buf,fgc,bgc,1);

}

void fullMenuDsp(){
    uint8_t begline=25;
    
    tft_fill_rect_blank(0,0,TFT_H,TFT_W);

    for(uint8_t m=0;m<MENU_NB;m++){
        lineMenuDsp(m,false);
    }
}

#define MENU_CODER_NB MENU_NB

volatile int16_t menuCoders[MENU_CODER_NB];  // [0] curr input nb ; [1] curr_input value
uint16_t maxMenuCoders[]={MENU_NB-1};

uint8_t coders_for_menu(){
    uint8_t currInput=0;
    uint8_t m=0;

    coderSetup(menuCoders,voicesSw,maxMenuCoders,1);

    fullMenuDsp();lineMenuDsp(0,true);

    while(1){

            for(uint8_t coder=0;coder<MENU_CODER_NB;coder++){

                int s=tst_switchs(coder,MENU_CODER_NB);            
                if(s>=0){return m;}

                uint32_t cc=menuCoders[coder];
                if(coder==0 && cc!=m){
                    lineMenuDsp(m,false);
                    m=cc;
                    lineMenuDsp(m,true);
                }
            }          
    }
}