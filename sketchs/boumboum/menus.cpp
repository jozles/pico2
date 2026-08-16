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
#include "sound_level_management.h"

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

extern volatile uint32_t millisCounter;

extern bool adsrScopeDisp[];

// mapping

void inputsInit(){
    memset(ctl_input_srce,0x00,MAX_INPUTS);
}

extern Voice voices[MAX_VOICES];

// frequences/ampl/lfos

volatile int16_t voicesWaveAmplCoders[MAX_VOICES][VCES_OUTPUTS_NB];

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

extern uint16_t* vcesVarF[];
uint16_t tempVceCoderFreq[MAX_VOICES];                      // pour accéder d'un ptr aux coders de freq
uint16_t tempVceCoderFreqAtt[MAX_VOICES];
uint16_t tempVceCoderCycleR[MAX_VOICES];
uint16_t tempVceCoderCycleRAtt[MAX_VOICES];
uint16_t tempVceCoderGenAmp[MAX_VOICES];
extern volatile int16_t menuVcesCodersF[];
extern int32_t voicesScopeDataBuffer[];

extern uint16_t* vcesVarM[];
uint16_t tempVcesCoderAmp[VCES_OUTPUTS_NB][MAX_VOICES];     // pour accéder d'un ptr aux ampl (sin, ampl tri etc)
extern volatile int16_t menuVcesCodersM[];

extern uint16_t* vcesVarT[];
uint16_t tempVcesCoderAtt[VCES_OUTPUTS_NB][MAX_VOICES];      // pour accéder d'un ptr aux att (sin, ampl tri etc)
extern volatile int16_t menuVcesCodersT[];

extern uint16_t adsrCoderAtt[MAX_ADSR];                      // current lfo freq
extern uint16_t adsrCoderDec[MAX_ADSR];
extern uint16_t adsrCoderSus[MAX_ADSR];
extern uint16_t adsrCoderRel[MAX_ADSR];
extern uint16_t adsrCoderLev[MAX_ADSR];
extern uint8_t  adsrStatus[MAX_ADSR];
extern uint32_t adsrCurrEch[MAX_ADSR];
extern volatile int16_t menuAdsrCoders[];
extern uint16_t* adsrVar[];
extern int32_t  adsrScopeBufReal[MAX_ADSR*ADSR_SCOPE_BUFFER_LEN];
extern int16_t  adsr_ctl_input_id[MAX_ADSR];

volatile bool codersSw[CODER_NB];                                    // coder it handler scans all physical coders
volatile bool codersTB[CODER_NB][OUTPUTS_STATES_NB];         // coder it handler scans all touchButtons

//extern uint16_t amplLevel[];                        // table des amplitudes

// mapping

#define MAPPING_CODER_NB 6
volatile int16_t mappingCoders[MAPPING_CODER_NB];   // [0] curr input nb ; [1] curr_input value ; [2] norm ; [3] shift ; [4] trig ; [5] trig level
uint16_t maxMappingCoders[]={MAX_INPUTS,MAX_OUTPUTS,3,2,4,0xffff};

// i2s

extern int32_t* i2s_buffer[];
extern int32_t* i2s_buf_scope;                      // last loaded buffer for scope
extern bool i2s_running;

// menu

#define SWIGNORE 1000
uint32_t swIgnore=millisCounter;
uint32_t tbIgnore=millisCounter+1;  // décale swIgnore/tbIgnore

#define LINE_LEN TFT_W/12+1
char buf[LINE_LEN];
char buf2[LINE_LEN];
char buf11x12[TFT_W/11+2];

uint16_t begline=34;    // first line after title

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
     OSCCODERUNUSED,    // pour décaler OSCGENAMP aussi utilisé avec Wavesamps et WavesAtt
     OSCGENAMP,
     OSCGENAMPATT
};

enum WavesAmps {
     OSCSINAMP,
     OSCTRIAMP,
     OSCSAWAMP,
     OSCSQRAMP,
     OSCWHAMP,
     OSCPNKAMP
};

enum WavesAtt {
     OSCSINAMPATT,
     OSCTRIAMPATT,
     OSCSAWAMPATT,
     OSCSQRAMPATT,
     OSCWHAMPATT,
     OSCPNKAMPATT  
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
        
        for(uint8_t a=0;a<VCES_OUTPUTS_NB;a++){
            //voices[v].coderSw[a]=true;
            tempVcesCoderAmp[a][v]=voices[v].coderWaveAmpl[a];
            tempVcesCoderAtt[a][v]=voices[v].coderWaveAmplAtt[a];
        }
        tempVceCoderFreq[v]=voices[v].coderFreq;
        tempVceCoderCycleR[v]=voices[v].coderCycleR;
        tempVceCoderCycleRAtt[v]=voices[v].coderCycleRAtt;
        tempVceCoderFreqAtt[v]=voices[v].coderFreqAtt;
        tempVceCoderGenAmp[v]=voices[v].genAmpl;    
    }
    // *** switchs ***
    for(uint8_t c=0;c<CODER_NB;c++){
        codersSw[c]=1;
        for(uint8_t s=0;s<OUTPUTS_STATES_NB;s++){
            codersTB[c][s]=false;}
    }
    // ***   osc   ***
    for(uint8_t c=0;c<CODER_NB;c++){
        lfosVar[c]=nullptr;
        vcesVarF[c]=nullptr;
        adsrVar[c]=nullptr;
    }
    // ***   lfos  ***
    lfosVar[OSCCODERFREQ-1]=lfosCodersFreq;     // lfosVar[0]
    lfosVar[OSCCODERCRA-1]=lfosCoderCycleR;     // lfosVar[1]
    lfosVar[OSCCODERFREQATT-1]=lfosCodersFreqAtt;     // lfosVar[2] (atténuateur de la valeur de l'input)  
    lfosVar[OSCCODERCRAATT-1]=lfosCoderCycleRAtt;     // lfosVar[3] (atténuateur de la valeur de l'input)    
    menuLfosCoders[OSCMENU]=0;                  // line 0 du menu
    menuLfosCoders[1]=lfosCodersFreq[0];
    menuLfosCoders[2]=lfosCoderCycleR[0];
    menuLfosCoders[3]=lfosCodersFreqAtt[0];     // (atténuateur de la valeur de l'input)
    menuLfosCoders[4]=lfosCoderCycleRAtt[0];    // (atténuateur de la valeur de l'input)
    // ***  voices Fr ***
    vcesVarF[OSCCODERFREQ-1]=tempVceCoderFreq;   // vcesVarF[0]
    vcesVarF[OSCCODERCRA-1]=tempVceCoderCycleR;  // vcesVarF[1]
    vcesVarF[OSCCODERCRAATT-1]=tempVceCoderCycleRAtt;
    vcesVarF[OSCCODERFREQATT-1]=tempVceCoderFreqAtt;
    vcesVarF[OSCGENAMP-1]=tempVceCoderGenAmp;    // vcesVarF[4]
    menuVcesCodersF[OSCMENU]=0;                  // line 0 du menu
    menuVcesCodersF[OSCCODERFREQ]=voices[0].coderFreq;
    menuVcesCodersF[OSCCODERCRA]=voices[0].coderCycleR;
    menuVcesCodersF[OSCCODERFREQATT]=voices[0].coderFreqAtt;    // (atténuateur de la valeur de l'input)
    menuVcesCodersF[OSCCODERCRAATT]=voices[0].coderCycleRAtt;   // (atténuateur de la valeur de l'input)
    menuVcesCodersF[OSCGENAMP]=voices[0].genAmpl;
        // ***  voices Amp ***
    vcesVarM[WSIN]=tempVcesCoderAmp[WSIN];   
    vcesVarM[WTRI]=tempVcesCoderAmp[WTRI];
    vcesVarM[WSAW]=tempVcesCoderAmp[WSAW];
    vcesVarM[WSQR]=tempVcesCoderAmp[WSQR];
    vcesVarM[WHIT]=tempVcesCoderAmp[WHIT];
    vcesVarM[PONK]=tempVcesCoderAmp[PONK];
    menuVcesCodersM[OSCMENU]=0;                  // line 0 du menu
    menuVcesCodersM[WSIN+1]=voices[0].basicWaveAmpl[WSIN];
    menuVcesCodersM[WTRI+1]=voices[0].basicWaveAmpl[WTRI];
    menuVcesCodersM[WSAW+1]=voices[0].basicWaveAmpl[WSAW];     
    menuVcesCodersM[WSQR+1]=voices[0].basicWaveAmpl[WSQR];
    menuVcesCodersM[WHIT+1]=voices[0].basicWaveAmpl[WHIT];
    menuVcesCodersM[PONK+1]=voices[0].basicWaveAmpl[PONK];
            // ***  voices Att ***
    vcesVarT[WSIN]=tempVcesCoderAtt[WSIN];   
    vcesVarT[WTRI]=tempVcesCoderAtt[WTRI];
    vcesVarT[WSAW]=tempVcesCoderAtt[WSAW];
    vcesVarT[WSQR]=tempVcesCoderAtt[WSQR];
    vcesVarT[WHIT]=tempVcesCoderAtt[WHIT];
    vcesVarT[PONK]=tempVcesCoderAtt[PONK];
    menuVcesCodersT[OSCMENU]=0;                  // line 0 du menu
    menuVcesCodersT[WSIN+1]=voices[0].coderWaveAmplAtt[WSIN];
    menuVcesCodersT[WTRI+1]=voices[0].coderWaveAmplAtt[WTRI];
    menuVcesCodersT[WSAW+1]=voices[0].coderWaveAmplAtt[WSAW];     
    menuVcesCodersT[WSQR+1]=voices[0].coderWaveAmplAtt[WSQR];
    menuVcesCodersT[WHIT+1]=voices[0].coderWaveAmplAtt[WHIT];
    menuVcesCodersT[PONK+1]=voices[0].coderWaveAmplAtt[PONK];
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

    char onoff[2]={'O','I'};
    memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
    memset(buf2,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
    switch(type){
        //case WAVES_AMP:sprintf(buf,"v:%d %4.3f amp",item,voices[item].frequency);break;

        case VOICES_FR:sprintf(buf,"%s:%u %1.3f%+i %c",title,item,voices[item].basicFrequency,voices[item].coderCycleR-MAXCODER_RC/2,onoff[i2s_running]);break;
        case VOICES_AM:sprintf(buf,"%s:%u %c",title,item,onoff[i2s_running]);break;
        case VOICES_AT:sprintf(buf,"%s:%u %c",title,item,onoff[i2s_running]);break;
        case LFOS_____:sprintf(buf,"%s:%u %1.3f%+i %c",title,item,lfosFrequency[item],lfosCoderCycleR[item]-MAXCODER_RC/2,onoff[i2s_running]);
                    sprintf(buf2,"crAt:%i frAt:%i",lfosCoderCycleRAtt[item],lfosCodersFreqAtt[item]);
                    break;        
        case ADSRL____:sprintf(buf,"%s%u %c",title,item,onoff[i2s_running]);
                    sprintf(buf2,"%u %u %u %+u %u",adsrCoderAtt[item],adsrCoderDec[item],adsrCoderSus[item],adsrCoderRel[item],adsrCoderLev[item]);break;
        //case MENU0:sprintf(buf,"%s  ",title);break;

        default:sprintf(buf,"%s %u %c    ",title,type,onoff[i2s_running]);break;
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
/*#define SCOPE_MODE -2
extern bool gpio_irq_set;
int8_t tst_switchs(uint8_t coder,uint8_t maxi){
    if((millisCounter-swIgnore)>=SWIGNORE){ 
        volatile int vs=codersSw[coder];          
        if(gpio_irq_set){
            swIgnore=millisCounter;
            gpio_irq_set=false;
            return -99;}      // button return
        if((volatile int)vs==0){        // coder[coder] on
            swIgnore=millisCounter;
            codersSw[coder]=1;
            if(coder<maxi){                      
                return coder;}          // coder number
            return -(coder-maxi+2);     // if maxi == 2 values are -2,-3,-4,-5
        }                               // if maxi == 4 values are -2,-3
    }
    return -1;                          // nothing
} */

// ****** switchs
#define SCOPE_MODE -2       
//extern bool gpio_irq_set;
int8_t tst_switchs_(uint8_t max_sw){      // return -1 if nothing, 0-n coder number, -99 return button 
    
    if((millisCounter-swIgnore)>=SWIGNORE){ 
        for(uint8_t c=0;c<max_sw;c++){
            volatile int vs=codersSw[c];
            if((volatile int)vs==0){    // coder[coder] on
                swIgnore=millisCounter;
                codersSw[c]=1;                     
                return c;               // coder number
            }      
        }
    }
    
    if((millisCounter-tbIgnore)>=SWIGNORE){
        for(uint8_t c=0;c<max_sw;c++){
            if(codersTB[c][RISE]){      // touchB[coder] on
                tbIgnore=millisCounter;
                codersTB[c][RISE]=false;                     
                return -99+c;           // coder number
            }                       
        }
    }                
    return -1;                          // nothing
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

    uint8_t cnt=0;
    
    bool mode_scope=false;
    
    uint8_t currInput=1;    // input for current cursor 
    uint8_t currDsp=0;      // line for current cursor

    mappingCoders[3]=ctl_input_shft[currInput];mappingCoders[4]=ctl_input_trig[currInput];mappingCoders[5]=ctl_input_tlev[currInput];

    coderSetup(mappingCoders,codersSw,codersTB,maxMappingCoders,3);
    
    //uint16_t ci=46;printf("menu_mapping id:%u %s %u \n",ci,ctl_input_name[ci],ctl_input_trig[ci]);

    fullMappingDsp(currInput,currDsp);

        while(1){

            ws_show_3(30);
            ledblinkn(2);
            //if(!mode_scope){test_st7789_2();}       // animation balayage de lignes
            //debug_ticker();

            //if(currInput==45 && cnt<3){cnt++;ci=46;printf("menu_mapping id:%u %s %u m:%u \n",ci,ctl_input_name[ci],ctl_input_trig[ci],mappingCoders[4]);}
            //if(currInput!=45){cnt=0;}

            for(uint8_t coder=0;coder<MAPPING_CODER_NB;coder++){        // coder 0 line ; coder 1 output ; coder 2 Shifted or not

                fillVoices();

                int s=tst_switchs_(MAPPING_CODER_NB);            
                if(s>=0 || s<=(-99+CODER_NB)){
                    // erase line 0 (tft_draw_text_11x12_dma_mult(0,line*((11+2))+FIRSTLINEH,buf11x12,fgc,bgc,1);)
                    tft_fill_rect_blank(FIRSTLINEH,0,11+3,TFT_W);
                    return s;}

                uint8_t cod=coder;if(coder>1){cod++;}
                uint32_t cc=mappingCoders[cod];
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
                            mappingCoders[1]=ctl_input_srce[currInput];mappingCoders[3]=ctl_input_shft[currInput];mappingCoders[4]=ctl_input_trig[currInput];mappingCoders[5]=ctl_input_tlev[currInput];
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
                            mappingCoders[1]=ctl_input_srce[currInput];mappingCoders[3]=ctl_input_shft[currInput];mappingCoders[4]=ctl_input_trig[currInput];mappingCoders[5]=ctl_input_tlev[currInput];
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
                if(coder==4 && cc!=ctl_input_tlev[currInput]){                   // coder 4 output trig level
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
            case MENU0____:sprintf(buf+2,"%s  ",menu+line*len);break; // général
            case LFOS_____:
                switch(coder){
                    case OSCMENU:
                        setLfosFrequency(calcFreq(lfosCodersFreq[line])/VOICE_FREQ_DIVIDER,line,lfosCoderCycleR[line]);
                        break;
                    case OSCCODERFREQ:if(varChge){                                          // cc=coder freq
                        setLfosFreq(line,cc);
                        }break;
                    case OSCCODERCRA:if(varChge){                                           // cc=coder cra
                        int16_t cra = ctl_input_val[lfo_ctl_input_id[line][LCR_]]>>10;      // normalisation ctl_input_cra 
                        int16_t cr = lfosCoderCycleR[line]+cra*cc/MAX_CTL_ATT;              // cr=coderCra+ctl_input_cra atténué 
                        setLfosFrequency(lfosFrequency[line],line,cr);
                        }break;
                    case OSCCODERFREQATT:if(varChge){                                       // cc=coder attenuator input freq 
                        setLfosFreqAtt(line,cc);
                        }break;
                    case OSCCODERCRAATT:if(varChge){                                        // cc=coder attenuator input cra
                        lfosCoderCycleRAtt[line]=cc;                                      
                        int16_t cra = ctl_input_val[lfo_ctl_input_id[line][LCR_]]>>10;      // normalisation ctl_input_cra
                        int16_t cr = lfosCoderCycleR[line]+cra*cc/MAX_CTL_ATT;              // cr=coderCra+ctl_input_cra atténué
                        printf("lfo#:%d cc(att):%d crinp:%i codCr:%i f_id:%d \n",line,cc,cra,cr,lfo_ctl_input_id[line][LCR_]);
                        signal_overflow("vce_cra:",line,cr,MINCODER_RC,MAXCODER_RC);
                        setLfosFrequency(lfosFrequency[line],line,cr);
                        }break;
                    default:break;
                }
                sprintf(buf+2,"%1.3f %1.3f %d   ",lfosFrequency[line],1/lfosFrequency[line],lfosCoderCycleR[line]-MAXCODER_RC/2);
                break;  
            case VOICES_FR:
                switch (coder){
                    case OSCMENU:{
                        float f=calcFreq(voices[line].coderFreq);
                        voices[line].basicFrequency=f;                
                        setVoiceFrequency(f,&voices[line],voices[line].coderCycleR);
                        }break;
                    case OSCCODERFREQ:if(varChge){
                        float f=calcFreq(cc);
                        voices[line].basicFrequency=f;                        
                        setVoiceFrequency(f,&voices[line],voices[line].coderCycleR);
                        voices[line].coderFreq=cc;}
                        break;
                    case OSCCODERCRA:if(varChge){
                        setVoiceFrequency(voices[line].frequency,&voices[line],cc);
                        voices[line].coderCycleR=cc;}
                        break;
                    case OSCCODERFREQATT:if(varChge){
                        setVoicesFreqAtt(line,cc);
                        }break;                                    
                    case OSCCODERCRAATT:if(varChge){
                        voices[line].coderCycleRAtt=cc;                                             // atténuateur pour ctl_input_cra
                        int16_t cra = ctl_input_val[voices[line].voice_ctl_input_id[LCR_]]>>10;     // normalisation ctl_input_cra
                        int16_t cr = voices[line].coderCycleR+cra*cc/MAX_CTL_ATT;                   // cr=coderCra+ctl_input_cra atténué 
                        signal_overflow("lfo_cra:",line,cr,MINCODER_RC,MAXCODER_RC);
                        setVoiceFrequency(voices[line].frequency,&voices[line],cr);}
                        break;
                    default: break;
                }

                sprintf(buf+2,"%4.3f %i %u %i %i ",voices[line].frequency,voices[line].coderCycleR-MAXCODER_RC/2,voices[line].coderGenAmpl,voices[line].coderFreqAtt,voices[line].coderCycleRAtt);               
                break;

            case VOICES_AM:          
                if(varChge){
                    voices[line].coderWaveAmpl[coder-1]=cc;
                    setVoicesAmpl(line,coder-1);              // !!! coders 0-6 (6=genAmpl)
                }
                              
                sprintf(buf+2,"%u %u %u %u %u %u %u",voices[line].coderWaveAmpl[WSIN],voices[line].coderWaveAmpl[WTRI],voices[line].coderWaveAmpl[WSAW],voices[line].coderWaveAmpl[WSQR],voices[line].coderWaveAmpl[WHIT],voices[line].coderWaveAmpl[PONK],voices[line].coderGenAmpl);               
                break;

            case VOICES_AT:      
                if(varChge){
                    voices[line].coderWaveAmplAtt[coder-1]=cc;
                    setVoicesAmpl(line,coder-1);                  // !!! coders 0-6 (6=genAmpl)
                }
           
                sprintf(buf+2,"%u %u %u %u %u %u %u",voices[line].coderWaveAmplAtt[WSIN],voices[line].coderWaveAmplAtt[WTRI],voices[line].coderWaveAmplAtt[WSAW],voices[line].coderWaveAmplAtt[WSQR],voices[line].coderWaveAmplAtt[WHIT],voices[line].coderWaveAmplAtt[PONK],voices[line].coderGenAmplAtt);               
                break;

            case ADSRL____:
                switch (coder){
                    case ADSRMENU: break;
                    case ADSRATT:adsrCoderAtt[line]=cc;setAdsrDur(line,ADSR_ATT,ctl_input_val[adsr_ctl_input_id[line]]);break;
                    case ADSRDEC:adsrCoderDec[line]=cc;setAdsrDur(line,ADSR_DEC,ctl_input_val[adsr_ctl_input_id[line]]);break;
                    case ADSRSUS:adsrCoderSus[line]=cc;setAdsrDur(line,ADSR_SUS,ctl_input_val[adsr_ctl_input_id[line]]);break;
                    case ADSRREL:adsrCoderRel[line]=cc;setAdsrDur(line,ADSR_REL,ctl_input_val[adsr_ctl_input_id[line]]);break;
                    case ADSRLEV:adsrCoderLev[line]=cc;setAdsrLev(line,cc+ctl_input_val[adsr_ctl_input_id[line]]);break;
                    default: break;
                }
                sprintf(buf+2,"%3u %3u %3u %3u %2u",adsrCoderAtt[line],adsrCoderDec[line],adsrCoderSus[line],adsrCoderRel[line],adsrCoderLev[line]);
                break;
            default:break;
        }

        if(!mode_scope){tft_draw_text_12x12_dma_mult(0,line*(12*2+1)+begline,buf,fgc,bgc,1);}
}

void fullMenuDsp(const char* title,const char* menu,uint8_t linesNb,uint8_t line_len,uint8_t currline,uint8_t type,uint8_t coder,uint32_t cc,bool mode_scope){

    tft_fill_rect_blank(0,0,TFT_H-begline,TFT_W);
    title_dsp(title,0,type);
    uint8_t bgl=0;          // first line to display
    if(type==0){bgl=1;}     // skip unused MENU0___ entry

    for(uint8_t l=bgl;l<linesNb;l++){
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
// les traitements associés à la modif de variables sont appelés depuis menuLineDsp() ou l'affichage de la ligne est décrit
// switch : la sortie est déclenchée soit par le "return button" soit par l'appui du coder 0 ; la valeur retournée est le n° de ligne
// les autres switchs passent en mode scope si le type de menu le gère ; coderNb indique le nombre de coders valides (coder 0 inclu)
uint8_t coders_for_menu(const char* title,const char* text,uint8_t linesNb,uint8_t line_len,uint8_t object_type,volatile int16_t* cTC,volatile bool* cTS, volatile bool (*cTB)[OUTPUTS_STATES_NB], uint16_t *maxi,uint16_t** var,uint8_t varNb,uint8_t switchsNb,uint8_t line0)
{

    uint8_t line=line0;
    bool mode_scope=false;
    uint8_t type_scope=0;
    bool firstScope=true;
    uint8_t wave=0;
    uint8_t debug=false;

    coderSetup(cTC,cTS,cTB,maxi,linesNb);   // coderSetup ignore lineNb

    fullMenuDsp(title,text,linesNb,line_len,line,object_type,0,0,false);

    //start adsr 0
    //adsrStatus[0]=ADSR_ATT

    while(1){
            
            fillVoices();        
            ws_show_3(30);
            ledblinkn(2);
            if(!mode_scope){test_st7789_2();}    // animation balayage de lignes            
            if(debug_ticker()){
                //if(adsrStatus[0]==ADSR_OFF){adsrStatus[0]=ADSR_ATT;adsrCurrEch[0]=0;}
            }          

            int s=tst_switchs_(switchsNb);
            //if(s<=(-99+CODER_NB)){i2s_start(!i2s_running);title_dsp(title,line,object_type);}           
            if(s==-99){i2s_start(!i2s_running);title_dsp(title,line,object_type);}           
            if(s==0){return line;}    // switch du coder 0 ou capaTouch
            if(s>0){                            // switch coders 1 à n
                mode_scope=true;firstScope=true;
                if((s-1)!=wave){type_scope=1;}
                else type_scope^=1;
                wave=s-1;
            } 

//printf("0\n");            
//if(object_type==VOICES_AM){printf("VOICES_AM\n");while(1){fillVoices();}}

            for(uint8_t coder=0;coder<varNb+1;coder++){       

                // sleep_ms(1); // needeed for coder stabilizes
                uint32_t cc=cTC[coder];
                if(object_type==MENU0____ && cc==0){cc=1;}   // skip unused MENU0____ entry

                // coder 0 : depl vertical
                if(coder==0 && cc!=line){
                                  
//printf("1_%u\n",cc);

                    menuLineDsp(text,line,line_len,false,object_type,coder,cc,mode_scope,NO_VAR_CHANGE);    // no reverse display
                    line=cc;
                    for(uint8_t k=0;k<varNb;k++){
                        if(var[k]!=nullptr){
                            cTC[k+1]=var[k][line];   // rechargement de la valeur actuelle des coder(1 à n, le 0 est pour le depl vertical) pour la nouvelle ligne ()
                        }
                    }
                    menuLineDsp(text,line,line_len,true,object_type,coder,cc,mode_scope,NO_VAR_CHANGE);     // reverse display
                    title_dsp(title,line,object_type);          
                }

                // coders 1 à n update variables des enregistrements
                if(varNb>0 && coder>0 && var[coder-1]!=nullptr){        // coder 0 pour depl vertical ; (ex lfos : coder 1 freq, coder 2 rc)

//printf("2_%u\n",cc);                    
                        if(var[coder-1][line]!=cc){                     // update coder value & display changes 
                            var[coder-1][line]=cc;
                            menuLineDsp(text,line,line_len,true,object_type,coder,cc,mode_scope,VAR_CHANGE);  // include values updates
                            title_dsp(title,line,object_type);
                        }
                }        
            }

            if(mode_scope){
                switch(object_type){
                    case LFOS_____:if(firstScope){title_dsp(title,line,LFOS_____);}    
                        scope(lfoScopeBufReal,lfosFrequency[line],begline,false,firstScope,0,wave,2,line);
                        firstScope=false;break;
                    
                    case VOICES_FR:
                        if(firstScope){title_dsp(title,line,VOICES_FR);}
                        if(type_scope==1){scope(voicesScopeDataBuffer+line*OSC_SCOPE_BUFFER_LEN,voices[line].frequency,begline,false,firstScope,0,wave,1);}
                        else{scope(i2s_buf_scope,voices[line].frequency,begline,false,firstScope,0,wave,0);}
                        firstScope=false;         
                        break;

                    case ADSRL____: 
                        if(adsrScopeDisp[line]==true){
                        //for(uint8_t w=0;w<TFT_W;w++){printf("%i\n",adsrScopeBufReal[w]);}
                            adsrScopeDisp[line]=false;
                            if(firstScope){title_dsp(title,line,ADSRL____);}
                            scope(&adsrScopeBufReal[line],0,begline,false,firstScope,0,0,3,line,1);
                        }
                        break;
                    default:break;
                }
            }          
    }
}