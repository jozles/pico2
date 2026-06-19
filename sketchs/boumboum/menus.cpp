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
#include "miscControls.h"
#include "bb_i2s.h"
#include "input_tables_management.h"

extern int32_t* i2s_buffer[];

extern int16_t  ctl_input_id[MAX_INPUTS];
extern int16_t  ctl_input_val[MAX_INPUTS];
extern char     ctl_input_name[MAX_INPUTS][IN_OUT_NAME_LEN];
extern uint16_t ctl_input_srce[MAX_INPUTS];
//extern uint16_t ctl_input_norm[MAX_INPUTS];
extern uint8_t  ctl_input_shft[MAX_INPUTS];                      // all inputs values shift type (0 no shift ; 1 +0x8000)
extern uint8_t  ctl_input_trig[MAX_INPUTS];                      // all inputs trig type (0 no trig ; 1 up ; 2 down ; 3 both)
extern int16_t  ctl_input_tlev[MAX_INPUTS];                      // all inputs trig level

extern char     ctl_output_name[MAX_OUTPUTS][IN_OUT_NAME_LEN];
extern int16_t  ctl_output_id_chain[MAX_OUTPUTS];

volatile uint32_t millisCounter=0;

// mapping

void inputsInit(){
    memset(ctl_input_srce,0x00,MAX_INPUTS);
}

extern Voice voices[MAX_VOICES];

// frequences/ampl/lfos

volatile int16_t voicesWaveAmplCoders[W_NB];
uint16_t voicesMaxWaveAmplCoders[W_NB];
volatile int16_t voicesFreqCoders[MAX_VOICES];
uint16_t voicesMaxFreqCoders[MAX_VOICES];
volatile int16_t voicesAmplCoders[MAX_VOICES];
uint16_t voicesMaxAmplCoders[MAX_VOICES];

extern float lfosFrequency[];                       // current lfo freq
extern uint16_t lfosCodersFreq[];
extern uint16_t lfosCodersFreqAtt[];
extern uint16_t lfosCoderCycleR[];
extern uint16_t lfosCoderCycleRAtt[];
extern uint16_t* lfosVar[];
extern int32_t lfoScopeBuffer[];
extern int32_t lfoScopeBufReal[];
extern int32_t* waveformTable[];
extern volatile int16_t menuLfosCoders[];
extern int16_t lfo_ctl_input_id[][MAX_OUTPUTS_PER_OBJ];

extern uint16_t* vcesVar[];
uint16_t tempVceCoderFreq[MAX_VOICES];
uint16_t tempVceCoderCycleR[MAX_VOICES];
uint16_t tempVceCoderGenAmp[MAX_VOICES];
extern volatile int16_t menuVcesCoders[];
extern int32_t voicesDataBuffer[];

extern uint16_t adsrCoderAtt[MAX_ADSR];                       // current lfo freq
extern uint16_t adsrCoderDec[MAX_ADSR];
extern uint16_t adsrCoderSus[MAX_ADSR];
extern uint16_t adsrCoderRel[MAX_ADSR];
extern uint16_t adsrCoderLev[MAX_ADSR];
extern uint8_t  adsrStatus[MAX_ADSR];
extern uint32_t adsrCurrEch[MAX_ADSR];
extern volatile int16_t menuAdsrCoders[];
extern uint16_t* adsrVar[];
extern int32_t  adsrScopeBufReal[MAX_ADSR*ADSR_SCOPE_BUFFER_LEN];

extern volatile bool voicesSw[];                    // coder it handler scans all physical coders

extern uint16_t amplLevel[];                        // table des amplitudes

// mapping

#define MAPPING_CODER_NB 6
volatile int16_t mappingCoders[MAPPING_CODER_NB];   // [0] curr input nb ; [1] curr_input value ; [2] norm ; [3] shift ; [4] trig ; [5] trig level
uint16_t maxMappingCoders[]={MAX_INPUTS,MAX_OUTPUTS,3,2,4,0xffff};

// i2s

extern int32_t* i2s_buf_scope;                      // last loaded buffer for scope

// menu

#define SWIGNORE 1000
uint32_t swIgnore=millisCounter;

#define LINE_LEN TFT_W/12+1
char buf[LINE_LEN];
char buf2[LINE_LEN];
char buf11x12[TFT_W/11+2];

uint16_t begline=34;

const char menu0_names[][MENU_NAME_LEN]={
    #define Z(name,text) text,
    #include "menu.def"
    #undef Z
};

/* ----------------------------------------- */
enum OscCoders {        // coders pour menu voices et lfos
     OSCMENU,
     OSCCODERFREQ,
     OSCCODERCRA,
     OSCCODERFREQATT,
     OSCCODERCRAATT,
     OSCGENAMP
};

enum AdsrCoders {        // coders pour menu adsr
     ADSRMENU,
     ADSRATT,
     ADSRDEC,
     ADSRSUS,
     ADSRREL,
     ADSRLEV
};

// ****** inits ******
void menus_init(){    
    for(uint8_t v=0;v<MAX_VOICES;v++){
        voicesFreqCoders[v]=voices[v].coderFreq; // 440Hz @1943 _ 1944 force first display
        voicesMaxFreqCoders[v]=voices[v].maxCoderFreq;
        voicesAmplCoders[v]=voices[v].coderGenAmpl;
        voicesMaxAmplCoders[v]=voices[v].maxCoderGenAmpl;
        //voices[v].coderAmpl[W_SINUS]=31;    // 5793
        //voices[v].basicWaveAmpl[W_SINUS]=amplLevel[voices[v].coderAmpl[W_SINUS]];
        
        for(uint8_t a=0;a<W_NB;a++){
            voices[v].coderSw[a]=true;
            voicesMaxWaveAmplCoders[a]=voices[v].maxCoderAmpl[a];   // voicesMaxWaveAmplCoders[W_NB] est un tableu pour le coderHandler
        }
        tempVceCoderFreq[v]=voices[v].coderFreq;
        tempVceCoderCycleR[v]=voices[v].coderCycleR;
        tempVceCoderGenAmp[v]=voices[v].genAmpl;    
    }
    // *** switchs ***
    for(uint8_t c=0;c<CODER_NB;c++){
        voicesSw[c]=1;
    }
    // ***   osc   ***
    for(uint8_t c=0;c<CODER_NB;c++){
        lfosVar[c]=nullptr;
        vcesVar[c]=nullptr;
        adsrVar[c]=nullptr;
    }
    // ***   lfos  ***
    lfosVar[OSCCODERFREQ-1]=lfosCodersFreq;     // lfosVar[0]
    lfosVar[OSCCODERCRA-1]=lfosCoderCycleR;     // lfosVar[1]
    lfosVar[OSCCODERCRAATT-1]=lfosCoderCycleRAtt;     // lfosVar[2] (atténuateur de la valeur de l'input)
    lfosVar[OSCCODERFREQATT-1]=lfosCodersFreqAtt;     // lfosVar[3] (atténuateur de la valeur de l'input)  
    menuLfosCoders[OSCMENU]=0;                  // line 0 du menu
    menuLfosCoders[1]=lfosCodersFreq[0];
    menuLfosCoders[2]=lfosCoderCycleR[0];
    menuLfosCoders[3]=lfosCodersFreqAtt[0];     // (atténuateur de la valeur de l'input)
    menuLfosCoders[4]=lfosCoderCycleRAtt[0];    // (atténuateur de la valeur de l'input)
    // ***  voices  ***
    vcesVar[OSCCODERFREQ-1]=tempVceCoderFreq;   // vcesVar[0]
    vcesVar[OSCCODERCRA-1]=tempVceCoderCycleR;  // vcesVar[1]
    vcesVar[OSCGENAMP-1]=tempVceCoderGenAmp;    // vcesVar[1]
    menuVcesCoders[OSCMENU]=0;                  // line 0 du menu
    menuVcesCoders[OSCCODERFREQ]=voices[0].coderFreq;
    menuVcesCoders[OSCCODERCRA]=voices[0].coderCycleR;
    menuVcesCoders[OSCCODERFREQATT]=voices[0].coderAttFreq;     // (atténuateur de la valeur de l'input)
    menuVcesCoders[OSCCODERCRAATT]=voices[0].coderCycleRAtt;    // (atténuateur de la valeur de l'input)
    menuVcesCoders[OSCGENAMP]=voices[0].genAmpl;
    // ***  Adsr  ****
    adsrVar[ADSRATT-1]=adsrCoderAtt;            // adsrVar[0]
    adsrVar[ADSRDEC-1]=adsrCoderDec;            // adsrVar[1]
    adsrVar[ADSRSUS-1]=adsrCoderSus;            // adsrVar[2]
    adsrVar[ADSRREL-1]=adsrCoderRel;            // adsrVar[3]
    adsrVar[ADSRLEV-1]=adsrCoderLev;            // adsrVar[4]
    menuAdsrCoders[ADSRMENU]=0;                 // line 0 du menu
    menuAdsrCoders[ADSRATT]=adsrCoderAtt[0];
    menuAdsrCoders[ADSRDEC]=adsrCoderDec[0];
    menuAdsrCoders[ADSRSUS]=adsrCoderSus[0];
    menuAdsrCoders[ADSRREL]=adsrCoderRel[0];
    menuAdsrCoders[ADSRLEV]=adsrCoderLev[0];
    
    mappingCoders[0]=0;     // ligne 0 
}

// ****** display title ******
void title_dsp(const char* title,uint8_t item,uint8_t type,float v0, int32_t v1, int32_t v2){

    memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
    memset(buf2,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
    switch(type){
        case AMPS:sprintf(buf,"v:%d %4.3f amp",item,voices[item].frequency);break;
        case LFOS:  sprintf(buf,"%s:%u %1.3f %i",title,item,lfosFrequency[item],lfosCoderCycleR[item]-MAXCODER_RC/2);
                    sprintf(buf2,"crAt:%i frAt:%i",lfosCoderCycleRAtt[item],lfosCodersFreqAtt[item]);
                    break;
        case VOICES:sprintf(buf,"%s:%u %1.3f%+i %i%i",title,item,voices[item].frequency,voices[item].coderCycleR-MAXCODER_RC/2,v1,v2);break;
        case ADSR:sprintf(buf,"%s:%u %u %u%+u %u %u ",title,item,adsrCoderAtt[item],adsrCoderDec[item],adsrCoderSus[item],adsrCoderRel[item],adsrCoderLev[item]);break;
        case MENU0:sprintf(buf,"%s  ",title);break;

        default:sprintf(buf,"%s           ",title);break;
    }        
    tft_draw_text_12x12_dma_mult(0,0,buf,0x001f,0x0000,1);
    tft_draw_text_12x12_dma_mult(0,14,buf2,0x001f,0x0000,1);
} 

void title_dsp(const char* title,uint8_t item,uint8_t type){
    title_dsp(title,item,type,0,0,0);
}

// ****** switchs obsolete
// return -1 if nothing, 0-n coder number, -99 return button 
// other values deprecated
#define SCOPE_MODE -2
extern bool gpio_irq_set;
int8_t tst_switchs(uint8_t coder,uint8_t maxi){
    if((millisCounter-swIgnore)>=SWIGNORE){ 
        volatile int vs=voicesSw[coder];          
        if(gpio_irq_set){
            swIgnore=millisCounter;
            gpio_irq_set=false;
            return -99;}      // button return
        if((volatile int)vs==0){        // coder[coder] on
            swIgnore=millisCounter;
            voicesSw[coder]=1;
            if(coder<maxi){                      
                return coder;}          // coder number
            return -(coder-maxi+2);     // if maxi == 2 values are -2,-3,-4,-5
        }                               // if maxi == 4 values are -2,-3
    }
    return -1;                          // nothing
} 

// ****** switchs
#define SCOPE_MODE -2       
extern bool gpio_irq_set;
int8_t tst_switchs_(uint8_t max_sw){      // return -1 if nothing, 0-n coder number, -99 return button 
    if((millisCounter-swIgnore)>=SWIGNORE){ 
        for(uint8_t c=0;c<max_sw;c++){
            volatile int vs=voicesSw[c];          
            if(gpio_irq_set){gpio_irq_set=false;return -99;}      // return button 
            if((volatile int)vs==0){    // coder[coder] on
                swIgnore=millisCounter;
                voicesSw[c]=1;                     
                return c;               // coder number
            }      
        }
    }
    return -1;                          // nothing
}

// ****** coders for waves ampl ******

uint8_t coders_for_wavesAmpl(uint8_t currVoice)
{ 
    bool mode_scope=false;
    bool firstScope=false;            

    volatile bool firstDisplay=true;

    tft_fill_rect_blank(begline,0,TFT_H-begline,TFT_W);
    title_dsp("",currVoice,AMPS);

    for(uint8_t a=0;a<W_NB;a++){voicesWaveAmplCoders[a]=voices[currVoice].coderAmpl[a];}
    coderSetup(voicesWaveAmplCoders,voicesSw,voicesMaxWaveAmplCoders,W_NB);    
    
    while (1) {

        fillVoices();
        
        ws_show_3(30);
        ledblinkn(2);
        if(!mode_scope){test_st7789_2();}       // animation balayage de lignes
        debug_ticker();

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
        //scope(int32_t* buf,float f,uint16_t begline,bool fd,bool blk,uint8_t refr,uint8_t wf)
        if(mode_scope && i2s_buf_scope!=nullptr){scope(voicesDataBuffer,voices[currVoice].frequency,begline,false,firstScope,3,0,1);firstScope=false;}   
    }
}

// ****** coders for mapping ******

#define NB_DSP_LINES 13
#define FIRSTLINEH 20

void mappingLineDsp(uint8_t inp,uint8_t line,bool rev){
    
    memset(buf11x12,0x00,LINE_LEN);
    convIntToString(buf11x12,(int32_t)inp,2);                           //  2 input#
    buf11x12[2]=' ';                                                    // +1
    uint8_t ln=IN_OUT_NAME_LEN-1;
    memcpy(buf11x12+3,&ctl_input_name[inp][0],ln);                      // +8 input name
    //printf("%s i:%d %s\n",buf,inp,ctl_input_name[inp]);

    buf11x12[3+ln]=' ';                                                 // +1
    memcpy(buf11x12+3+ln+1,&ctl_output_name[ctl_input_srce[inp]],9);    // +7 //IN_OUT_NAME_LEN);
    uint16_t fgc=0x07EF;
    uint16_t bgc=0x0000;
    uint16_t buc=fgc;
    if(rev){fgc=bgc;bgc=buc;}

    tft_draw_text_11x12_dma_mult(0,line*((11+2))+FIRSTLINEH,buf11x12,fgc,bgc,1);
}

void fullMappingDsp(uint8_t firstInput,uint8_t currDspInput){
    tft_fill_rect_blank(FIRSTLINEH,0,TFT_H,TFT_W);
    for(uint8_t l=0;l<NB_DSP_LINES;l++){
        while(ctl_input_name[firstInput+l][0]==0 && firstInput+l<MAX_INPUTS){firstInput++;}
        fillVoices();
        mappingLineDsp(firstInput+l,l,currDspInput==l);
    }
}

uint8_t coders_for_mapping(){

    bool mode_scope=false;
    
    uint8_t currInput=1;    // input for current cursor 
    uint8_t currDsp=0;      // line for current cursor

    coderSetup(mappingCoders,voicesSw,maxMappingCoders,3);

    fullMappingDsp(currInput,currDsp);

        while(1){

            ws_show_3(30);
            ledblinkn(2);
            if(!mode_scope){test_st7789_2();}       // animation balayage de lignes
            debug_ticker();

            for(uint8_t coder=0;coder<MAPPING_CODER_NB;coder++){        // coder 0 line ; coder 1 output ; coder 2 Shifted or not

                fillVoices();

                int s=tst_switchs_(MAPPING_CODER_NB);            
                if(s>=0 || s<=-99){
                    // erase line 0 (tft_draw_text_11x12_dma_mult(0,line*((11+2))+FIRSTLINEH,buf11x12,fgc,bgc,1);)
                    tft_fill_rect_blank(FIRSTLINEH,0,11+3,TFT_W);
                    return s;}

                uint32_t cc=mappingCoders[coder];
                if(coder==0){                                           // coder 0 vertical movements

                        if(currInput<MAX_INPUTS-1 && cc>currInput){             // cursor moves down
                                              
                            if(currDsp<NB_DSP_LINES-1){                         // no scroll                                
                                mappingLineDsp(currInput,currDsp,false);        // restore prev
                                currDsp++;currInput++;
                                while(ctl_input_name[currInput][0]==0 && currInput<MAX_INPUTS-1){currInput++;}
                                mappingLineDsp(currInput,currDsp,true);    
                            }
                            else {                                              // scroll down
                                currInput++;                                    
                                while(ctl_input_name[currInput][0]==0 && currInput<MAX_INPUTS-1){currInput++;} // get next Input to display
                                uint8_t schdInput=currInput;
                                for(uint8_t l=0;l<NB_DSP_LINES-1;l++){          // search first displayable input
                                    // INPUT 1 MUST BE DISPLAYABLE
                                    schdInput--;
                                    while(ctl_input_name[schdInput][0]==0 && schdInput>1){schdInput--;}
                                }                                         
                                fullMappingDsp(schdInput,currDsp);
                            }
                            mappingCoders[1]=ctl_input_srce[currInput];
                        }
                        else if(currInput>1 && cc<currInput){                   // cursor moves up
                                               
                            if(currDsp>0){                                      // no scroll
                                mappingLineDsp(currInput,currDsp,false);        // restore prev
                                currDsp--;currInput--;
                                while(ctl_input_name[currInput][0]==0 && currInput>1){currInput--;}
                                mappingLineDsp(currInput,currDsp,true);    
                            }
                            else {                                              // scroll up
                                currInput--;
                                while(ctl_input_name[currInput][0]==0 && currInput>1){currInput--;}
                                fullMappingDsp(currInput,currDsp);                                
                            }
                            mappingCoders[1]=ctl_input_srce[currInput];
                        }
                        mappingCoders[0]=currInput;
                }
                if(coder==1 && cc!=ctl_input_srce[currInput]){                   // coder 1 output choice 
                    
                    if(ctl_output_name[cc][0]=='-'){                             // search valid output
                        
                        if(cc>ctl_input_srce[currInput]){
                            while(ctl_output_name[cc][0]=='-' && cc<MAX_OUTPUTS-1){cc++;}
                            if(ctl_output_name[cc][0]=='-'){cc=ctl_input_srce[currInput];continue;}   
                        }
                        else {
                            while(ctl_output_name[cc][0]=='-' && cc>0){cc--;}
                        }  
                    }
                    if(cc<(MAX_OUTPUTS-1) && (cc>=0)){                           // cc output sélectionnée

                        disconnect_input(currInput,ctl_input_srce[currInput]);   // previous srce output
                        mappingCoders[coder]=cc;                                 // valid output store
                        ctl_input_srce[currInput]=cc;                            // update srce output
                        connect_input(currInput,ctl_input_srce[currInput]);      // connect new srce

                        mappingLineDsp(currInput,currDsp,true);
                        printf("(%c)out#(cc):%d out_chain:%d input#:%d src:%d \n",ctl_output_name[cc][0],cc,ctl_output_id_chain[ctl_input_srce[currInput]],currInput,ctl_input_srce[currInput]);
                    }
                }
                if(coder==2 && cc!=ctl_input_shft[currInput]){                   // coder 3 output shift
                    ctl_input_shft[currInput]=cc;
                    mappingLineDsp(currInput,currDsp,true);
                }    
                if(coder==3 && cc!=ctl_input_trig[currInput]){                   // coder 4 output trig
                    ctl_input_trig[currInput]=cc;
                    mappingLineDsp(currInput,currDsp,true);
                }                    
                if(coder==4 && cc!=ctl_input_tlev[currInput]){                   // coder 5 output trig level
                    ctl_input_tlev[currInput]=cc;
                    mappingLineDsp(currInput,currDsp,true);
                }                    
            }
        }          
}

// ****** coders for menu ******
#define NO_VAR_CHANGE   false       // pas de modif de variables
#define VAR_CHANGE      true

void menuLineDsp(const char* menu,uint8_t line,uint8_t len,bool rev,uint8_t type,uint8_t coder,uint32_t cc,bool mode_scope,bool varChge)
{
//printf("line:%u rev:%u type:%u ",line,rev);    
        uint16_t fgc=GREEN;
        uint16_t bgc=0x0000;
        uint16_t buc=fgc;
        if(rev){fgc=bgc;bgc=buc;}
        buf[0]=line+48;
        buf[1]=' ';

        switch(type){
            case MENU0:sprintf(buf+2,"%s  ",menu+line*len);break; // général
            case LFOS:
                switch(coder){
                    case OSCMENU:
                        setLfosFrequency(calcFreq(lfosCodersFreq[line])/VOICE_FREQ_DIVIDER,line,lfosCoderCycleR[line]);
                        break;
                    case OSCCODERFREQ:if(varChge){                                          // cc=coder freq
                        int16_t fi = ctl_input_val[lfo_ctl_input_id[line][VFRQ]]>>3;        // normalisation ctl_input_freq 
                        uint16_t fc = cc+fi*lfosCodersFreqAtt[line]/MAX_CTL_ATT;            // fc=coderFreq+ctl_input_freq atténué                
                        setLfosFrequency(calcFreq(fc)/VOICE_FREQ_DIVIDER,line,lfosCoderCycleR[line]);}
                        break;
                    case OSCCODERCRA:if(varChge){                                           // cc=coder cra
                        int16_t cra = ctl_input_val[lfo_ctl_input_id[line][VCRA]]>>10;      // normalisation ctl_input_cra 
                        int16_t cr = lfosCoderCycleR[line]+cra*cc/MAX_CTL_ATT;              // cr=coderCra+ctl_input_cra atténué 
                        setLfosFrequency(lfosFrequency[line],line,cr);}
                        break;
                    case OSCCODERFREQATT:if(varChge){                                       // cc=coder attenuator input freq          
                        lfosCodersFreqAtt[line]=cc;                                        
                        int16_t fi = ctl_input_val[lfo_ctl_input_id[line][VFRQ]]>>3;        // normalisation ctl_input_freq
                        int16_t fc = lfosCodersFreq[line]+fi*cc/MAX_CTL_ATT;                // fc=coderFreq+ctl_input_freq atténué
                        printf("lfo#:%d cc(att):%d finp:%i fc:%i f_id:%d \n",line,cc,fi,fc,lfo_ctl_input_id[line][VFRQ]);
                        signal_overflow("vce_freq:",line,fc,LFOS_MIN_FREQ_CODERS,LFOS_MAX_FREQ_CODERS);
                        setLfosFrequency(calcFreq(fc)/VOICE_FREQ_DIVIDER,line,lfosCoderCycleR[line]);}
                        break;
                    case OSCCODERCRAATT:if(varChge){                                        // cc=coder attenuator input cra
                        lfosCoderCycleRAtt[line]=cc;                                      
                        int16_t cra = ctl_input_val[lfo_ctl_input_id[line][VCRA]]>>10;      // normalisation ctl_input_cra
                        int16_t cr = lfosCoderCycleR[line]+cra*cc/MAX_CTL_ATT;              // cr=coderCra+ctl_input_cra atténué
                        printf("lfo#:%d cc(att):%d crinp:%i codCr:%i f_id:%d \n",line,cc,cra,cr,lfo_ctl_input_id[line][VCRA]);
                        signal_overflow("vce_cra:",line,cr,MINCODER_RC,MAXCODER_RC);
                        setLfosFrequency(lfosFrequency[line],line,cr);}
                        break;
                    default:break;
                }
                sprintf(buf+2,"%1.3f %1.3f %d   ",lfosFrequency[line],1/lfosFrequency[line],lfosCoderCycleR[line]-MAXCODER_RC/2);
                break;  
            case VOICES:
                switch (coder){
                    case OSCMENU:                
                        setVoiceFrequency(calcFreq(voices[line].coderFreq),&voices[line],voices[line].coderCycleR);
                        break;
                    case OSCCODERFREQ:if(varChge){
                        setVoiceFrequency(calcFreq(cc),&voices[line],voices[line].coderCycleR);
                        voices[line].coderFreq=cc;}
                        break;
                    case OSCCODERCRA:if(varChge){
                        setVoiceFrequency(voices[line].frequency,&voices[line],cc);
                        voices[line].coderCycleR=cc;}
                        break;
                    case OSCGENAMP:if(varChge){
                        setVoiceFrequency(voices[line].frequency,&voices[line],voices[line].coderCycleR);
                        voices[line].coderGenAmpl=cc;voices[line].genAmpl=amplLevel[cc];}
                        break; 
                    case OSCCODERFREQATT:if(varChge){
                        voices[line].coderAttFreq=cc;                                               // atténuateur pour ctl_input_freq 
                        int16_t fi = ctl_input_val[voices[line].voice_ctl_input_id[VFRQ]]>>3;       // normalisation ctl_input_freq
                        uint32_t fc = voices[line].coderFreq+fi*cc/MAX_CTL_ATT;                     // fc=coderFreq+ctl_input_freq atténué 
                        signal_overflow("lfo_freq:",line,fc,VCES_MIN_FREQ_CODERS,VCES_MAX_FREQ_CODERS);
                        setVoiceFrequency(calcFreq(fc),&voices[line],voices[line].coderCycleR);}
                        break;                                    
                    case OSCCODERCRAATT:if(varChge){
                        voices[line].coderCycleRAtt=cc;                                             // atténuateur pour ctl_input_cra
                        int16_t cra = ctl_input_val[voices[line].voice_ctl_input_id[VCRA]]>>10;     // normalisation ctl_input_cra
                        int16_t cr = voices[line].coderCycleR+cra*cc/MAX_CTL_ATT;                   // cr=coderCra+ctl_input_cra atténué 
                        signal_overflow("lfo_cra:",line,cr,MINCODER_RC,MAXCODER_RC);
                        setVoiceFrequency(voices[line].frequency,&voices[line],cr);}
                        break;
                    default:break;
                }
                sprintf(buf+2,"%4.3f %i %u",voices[line].frequency,voices[line].coderCycleR-MAXCODER_RC/2,voices[line].coderGenAmpl);               
                break;
            case ADSR:
                switch (coder){
                    case ADSRMENU: break;
                    case ADSRATT:adsrCoderAtt[line]=cc;setAdsrDur(line,ADSR_ATT,cc);break;
                    case ADSRDEC:adsrCoderDec[line]=cc;setAdsrDur(line,ADSR_DEC,cc);break;
                    case ADSRSUS:adsrCoderSus[line]=cc;setAdsrDur(line,ADSR_SUS,cc);break;
                    case ADSRREL:adsrCoderRel[line]=cc;setAdsrDur(line,ADSR_REL,cc);break;
                    case ADSRLEV:adsrCoderLev[line]=cc;setAdsrLev(line,cc);break;
                    default: break;
                }
                sprintf(buf+2,"%3u %3u %3u %3u %2u",adsrCoderAtt[line],adsrCoderDec[line],adsrCoderSus[line],adsrCoderRel[line],adsrCoderLev[line]);
                break;
            default:break;
        }
//printf("line:%u \n",line);
        if(!mode_scope){tft_draw_text_12x12_dma_mult(0,line*(12*2+1)+begline,buf,fgc,bgc,1);}
}

void fullMenuDsp(const char* title,const char* menu,uint8_t linesNb,uint8_t line_len,uint8_t currline,uint8_t type,uint8_t coder,uint32_t cc,bool mode_scope){

    tft_fill_rect_blank(begline,0,TFT_H-begline,TFT_W);
    title_dsp(title,0,type);

    for(uint8_t l=0;l<linesNb;l++){
        fillVoices();
        printf("beg:%d m:%s l:%d len:%d cl:%d type:%d cod:%d\n",begline,menu+line_len*l,l,line_len,currline==l,type,coder);//,cc,mode_scope,NO_VAR_CHANGE);
        menuLineDsp(menu,l,line_len,currline==l,type,coder,cc,mode_scope,NO_VAR_CHANGE);
    }
}

// ****** coders_for_menu() ****** affiche un menu avec ligne courante en reverse avec des saisies optionnelles ; 
// si les lignes ont un libellé, text pointe sur le tableau[lineNb,line_len] ; lineNb nombre de lignes du menu et line_len la longueur du libellé
// si aucune saisie/variables c'est le type 0 (cTC[0] contient la valeur courante du 1er coder et maxi le nombre de lignes à afficher-1) - voir menu0
// s'il y a des variables à afficher c'est un type!=0 : créer l'enum du type et une ligne d'affichage dans menuLineDsp
// s'il y a des variables à saisir via coder, uint16_t* var[] contient les pointeurs sur les tableaux uint16_t[line] (valeur courante du coder correspondant) voir comments dans boumboum.cpp
// donc var[coder-1][line] permet d'accéder à ces valeurs de coder (traitement spécifique éventuel selon le type dans menuLineDsp (ou ailleurs)
// varNb est le nombre de variables 
// les traitements associés à lamodif de variables sont appelés depuis menuLineDsp() ou l'affichage de la ligne est décrit
// switch : la sortie est déclenchée soit par le "return button" soit par l'appui du coder 0 ; la valeur retournée est le n° de ligne
// les autres switchs passent en mode scope si le type de menu le gère ; coderNb indique le nombre de coders valides (coder 0 inclu)
uint8_t coders_for_menu(const char* title,const char* text,uint8_t linesNb,uint8_t line_len,uint8_t type,volatile int16_t *cTC, volatile bool *cTS, uint16_t *maxi,uint16_t** var,uint8_t varNb,uint8_t switchsNb,uint8_t line0)
{

    uint8_t line=line0;
    bool mode_scope=false;
    uint8_t type_scope=0;
    bool firstScope=true;
    uint8_t wave=0;
    uint8_t debug=false;

    coderSetup(cTC,cTS,maxi,linesNb);   // coderSetup ignore lineNb

    fullMenuDsp(title,text,linesNb,line_len,line,type,0,0,false);

    // init adsr 0
    adsrCoderAtt[0]=94;setAdsrDur(0,ADSR_ATT,94);
    adsrCoderDec[0]=110;setAdsrDur(0,ADSR_DEC,120);
    adsrCoderSus[0]=110; //setAdsrDur(0,ADSR_SUS,110);
    adsrCoderRel[0]=118; //setAdsrDur(0,ADSR_REL,118);
    adsrCoderLev[0]=26;  //setAdsrLev(0,26);
    bool adsrNew=false;

    while(1){
            
            fillVoices();
        
            ws_show_3(30);
            ledblinkn(2);
            if(!mode_scope){test_st7789_2();}    // animation balayage de lignes
            
            if(debug_ticker()){if(adsrStatus[0]==ADSR_OFF){adsrStatus[0]=ADSR_ATT;adsrCurrEch[0]=0;adsrNew=true;}};          

            int s=tst_switchs_(switchsNb);            
            if(s==0 || s==-99){return line;}
            if(s>0){
                mode_scope=true;firstScope=true;
                if((s-1)!=wave){type_scope=1;}
                else type_scope^=1;
                wave=s-1;
            }    

            for(uint8_t coder=0;coder<varNb+1;coder++){       

                // sleep_ms(1); // needeed for coder stabilizes
                uint32_t cc=cTC[coder];
                
                // coder 0 : depl vertical
                if(coder==0 && cc!=line){
                                  
                    menuLineDsp(text,line,line_len,false,type,coder,cc,mode_scope,NO_VAR_CHANGE);    // no reverse display
                    line=cc;
                    for(uint8_t k=0;k<varNb;k++){
                        if(var[k]!=nullptr){
                            cTC[k+1]=var[k][line];   // rechargement de la valeur actuelle des coder(1 à n, le 0 est pour le depl vertical) pour la nouvelle ligne ()
                        }
                    }
                    menuLineDsp(text,line,line_len,true,type,coder,cc,mode_scope,NO_VAR_CHANGE);     // reverse display
                    title_dsp(title,line,type);          
                }

                // coders 1 à n update variables des enregistrements
                if(varNb>0 && coder>0 && var[coder-1]!=nullptr){        // coder 0 pour depl vertical ; (ex lfos : coder 1 freq, coder 2 rc)
                        if(var[coder-1][line]!=cc){                     // update coder value & display changes 
                            var[coder-1][line]=cc;
                            menuLineDsp(text,line,line_len,true,type,coder,cc,mode_scope,VAR_CHANGE);  // include values updates
                            title_dsp(title,line,type);
                        }
                }        
            }
            if(mode_scope){
                if(type==LFOS){
                    if(firstScope){title_dsp(title,line,LFOS);}    
                    //if(type_scope){scope(&lfoScopeBuffer[line*OSC_SCOPE_BUFFER_LEN],lfosFrequency[line],begline,false,firstScope,0,wave,1);firstScope=false;}
                    //else{
                        //for(uint8_t i=0;i<TFT_W;i++){printf("ptr:%d c0:%i c1:%i c2:%i c3:%i\n",i,lfoScopeBufReal[0*OSC_SCOPE_BUFFER_LEN*BASIC_WAVES_NB + i*BASIC_WAVES_NB+LSIN]);}
                        scope(lfoScopeBufReal,lfosFrequency[line],begline,false,firstScope,0,wave,2,line);
                    //}
                    firstScope=false;       
                }
                else if(type==VOICES){ 
                    if(firstScope){title_dsp(title,line,VOICES,0,wave,type_scope);}
                    if(type_scope==1){scope(voicesDataBuffer+line*OSC_SCOPE_BUFFER_LEN,voices[line].frequency,begline,false,firstScope,0,wave,1);}
                    else{scope(i2s_buf_scope,voices[line].frequency,begline,false,firstScope,0,wave,0);}
                    firstScope=false;         
                }
                else if(type==ADSR && adsrNew==true){
                    adsrNew=false;
                    if(firstScope){title_dsp(title,line,ADSR,0,wave,type_scope);}
                    scope(&adsrScopeBufReal[line],0,begline,false,firstScope,0,0,3,line);
                }
                else mode_scope=false;
            }          
    }
                        //scope(i2s_buffer[0],voices[0].frequency,14,false,true,0,0,false);firstScope=false;     // scope mode_data
                        //printf("i2s_buffer f:%f rc:%i ampl:%d\n",voices[0].frequency,voices[0].coderCycleR,voices[0].basicWaveAmpl[W_SINUS]);delay_ms(100);
                        //dumpStr(i2s_buffer[0],256);    
}