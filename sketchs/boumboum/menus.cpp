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

extern char     in_table_name[MAX_INPUTS][IN_OUT_NAME_LEN];
extern uint16_t in_table_srce[MAX_INPUTS];
extern char     out_table_name[MAX_OUTPUTS][IN_OUT_NAME_LEN];


volatile uint32_t millisCounter=0;

// mapping

/*const char inputs_names[][IN_OUT_NAME_LEN]={
    #define X(name,text) text,
    #include "inputs.def"   
    #undef X   
};*/

/*const char out_table_names[][IN_OUT_NAME_LEN]={
    #define Y(name,text) text,
    #include "outputs.def"
    #undef Y
};*/

void inputsInit(){
    memset(in_table_srce,0x00,MAX_INPUTS);
}

extern Voice voices[MAX_VOICES];

//Lfo lfos[MAX_LFO];

// frequences/ampl/lfos

volatile int16_t voicesWaveAmplCoders[W_NB];
uint16_t voicesMaxWaveAmplCoders[W_NB];
volatile int16_t voicesFreqCoders[MAX_VOICES];
uint16_t voicesMaxFreqCoders[MAX_VOICES];
volatile int16_t voicesAmplCoders[MAX_VOICES];
uint16_t voicesMaxAmplCoders[MAX_VOICES];

extern float lfosFrequency[];                       // current lfo freq
extern uint16_t lfosCoders[];
extern uint16_t lfosCoderCycleR[];
extern uint16_t* lfosVar[];
extern int32_t lfoScopeBuffer[];
extern int32_t* waveformTable[];
extern volatile int16_t menuLfosCoders[];

extern uint16_t* vcesVar[];
uint16_t tempVceCoderFreq[MAX_VOICES];
uint16_t tempVceCoderCycleR[MAX_VOICES];
uint16_t tempVceCoderGenAmp[MAX_VOICES];
extern volatile int16_t menuVcesCoders[];
extern int32_t voicesDataBuffer[];

extern uint16_t adsrAttCoder[MAX_ADSR];                       // current lfo freq
extern uint16_t adsrDecCoder[MAX_ADSR];
extern uint16_t adsrSusCoder[MAX_ADSR];
extern uint16_t adsrRelCoder[MAX_ADSR];
extern uint16_t adsrLevCoder[MAX_ADSR];
extern volatile int16_t menuAdsrCoders[];
extern uint16_t* adsrVar[];

extern volatile bool voicesSw[];                    // coder it handler scans all physical coders

extern uint16_t amplLevel[];                        // table des amplitudes

// mapping

#define MAPPING_CODER_NB 2
volatile int16_t mappingCoders[MAPPING_CODER_NB];   // [0] curr input nb ; [1] curr_input value
uint16_t maxMappingCoders[]={MAX_INPUTS,MAX_OUTPUTS};

// i2s

extern int32_t* i2s_buf_scope;                      // last loaded buffer for scope

// menu

#define SWIGNORE 1000
uint32_t swIgnore=millisCounter;

#define LINE_LEN TFT_W/12+1
char buf[LINE_LEN];
char buf11x12[TFT_W/11+1];

uint16_t begline=27;

const char menu0_names[][MENU_NAME_LEN]={
    #define Z(name,text) text,
    #include "menu.def"
    #undef Z
};

/* ----------------------------------------- */
enum OscCoders {        // coders pour menu voices et lfos
     OSCMENU,
     OSCCODERFREQ,
     OSCCODERRC,
     OSCGENAMP
};

enum AdsrCoders {        // coders pour menu voices et lfos
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
    }
    // ***   lfos  ***
    lfosVar[OSCCODERFREQ-1]=lfosCoders;         // lfosVar[0]
    lfosVar[OSCCODERRC-1]=lfosCoderCycleR;      // lfosVar[1]
    menuLfosCoders[OSCMENU]=0;                  // line 0 du menu
    menuLfosCoders[1]=lfosCoders[0];
    menuLfosCoders[2]=lfosCoderCycleR[0];
    // ***  voices  ***
    vcesVar[OSCCODERFREQ-1]=tempVceCoderFreq;   // vcesVar[0]
    vcesVar[OSCCODERRC-1]=tempVceCoderCycleR;   // vcesVar[1]
    vcesVar[OSCGENAMP-1]=tempVceCoderGenAmp;    // vcesVar[1]
    menuVcesCoders[OSCMENU]=0;                  // line 0 du menu
    menuVcesCoders[OSCCODERFREQ]=voices[0].coderFreq;
    menuVcesCoders[OSCCODERRC]=voices[0].coderCycleR;
    menuVcesCoders[OSCGENAMP]=voices[0].genAmpl;
    // ***  Adsr  ****
    adsrVar[ADSRATT-1]=adsrAttCoder;            // lfosVar[0]
    adsrVar[ADSRDEC-1]=adsrDecCoder;            // lfosVar[1]
    adsrVar[ADSRSUS-1]=adsrSusCoder;            // lfosVar[2]
    adsrVar[ADSRREL-1]=adsrRelCoder;            // lfosVar[3]
    adsrVar[ADSRLEV-1]=adsrLevCoder;            // lfosVar[4]
    menuAdsrCoders[ADSRMENU]=0;                 // line 0 du menu
    menuAdsrCoders[ADSRATT]=adsrAttCoder[0];
    menuAdsrCoders[ADSRDEC]=adsrDecCoder[0];
    menuAdsrCoders[ADSRSUS]=adsrSusCoder[0];
    menuAdsrCoders[ADSRREL]=adsrRelCoder[0];
    menuAdsrCoders[ADSRLEV]=adsrLevCoder[0];
    
    mappingCoders[0]=0;     // ligne 0 
}

// ****** display title ******
void title_dsp(const char* title,uint8_t item,uint8_t type,float v0, float v1, uint32_t v2){

    memset(buf,0x20,LINE_LEN);buf[LINE_LEN-1]=0x00;
    switch(type){
        case AMPS:sprintf(buf,"v:%d %4.3f amp",item,voices[item].frequency);break;
        case LFOS:sprintf(buf,"%s:%u %1.3f %i ",title,item,lfosFrequency[item],lfosCoderCycleR[item]-MAXCODER_RC/2);break;
        case VOICES:sprintf(buf,"%s:%u %1.3f %i ",title,item,voices[item].frequency,voices[item].coderCycleR-MAXCODER_RC/2);break;
        case ADSR:sprintf(buf,"%s:%u %u %u %u %u %u ",title,item,adsrAttCoder[item],adsrDecCoder[item],adsrSusCoder[item],adsrRelCoder[item],adsrLevCoder[item]);break;

        default:sprintf(buf,"%s     ",title);break;
    }        
    tft_draw_text_12x12_dma_mult(0,0,buf,0x001f,0x0000,1);
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
// return -1 if nothing, 0-n coder number, -99 return button 
#define SCOPE_MODE -2
extern bool gpio_irq_set;
int8_t tst_switchs_(uint8_t maxi){
    if((millisCounter-swIgnore)>=SWIGNORE){ 
        for(uint8_t c=0;c<maxi;c++){
            volatile int vs=voicesSw[c];          
            if(gpio_irq_set){gpio_irq_set=false;return -99;}      // button return
            if((volatile int)vs==0){        // coder[coder] on
                swIgnore=millisCounter;
                voicesSw[c]=1;                     
                return c;          // coder number
            }                              // if maxi == 4 values are -2,-3
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
        if(mode_scope && i2s_buf_scope!=nullptr){scope(voicesDataBuffer,voices[currVoice].frequency,begline,false,firstScope,3,0,true);firstScope=false;}   
    }
}

// ****** coders for mapping ******

#define NB_DSP_LINES 12
#define FIRSTLINEH 20

void mappingLineDsp(uint8_t inp,uint8_t line,bool rev){
    
    memset(buf11x12,0x00,LINE_LEN);
    convIntToString(buf11x12,(int32_t)inp,2);                        //  2 input#
    buf11x12[2]=' ';                                                 // +1
    uint8_t ln=IN_OUT_NAME_LEN-2;
    memcpy(buf11x12+3,&in_table_name[inp][0],ln);                    // +8 input name
    //printf("%s i:%d %s\n",buf,inp,in_table_name[inp]);

    buf11x12[3+ln]=' ';                                              // +1
    memcpy(buf11x12+3+ln+1,&out_table_name[in_table_srce[inp]],9);   // +7 //IN_OUT_NAME_LEN);
    uint16_t fgc=0x07EF;
    uint16_t bgc=0x0000;
    uint16_t buc=fgc;
    if(rev){fgc=bgc;bgc=buc;}
    //tft_draw_text_12x12_dma_mult(0,line*((12+2))+FIRSTLINEH,buf,fgc,bgc,1);
    tft_draw_text_11x12_dma_mult(0,line*((11+2))+FIRSTLINEH,buf11x12,fgc,bgc,1);
}

void fullMappingDsp(uint8_t firstInput,uint8_t currDspInput){
    tft_fill_rect_blank(FIRSTLINEH,0,TFT_H,TFT_W);
    for(uint8_t l=0;l<NB_DSP_LINES;l++){
        mappingLineDsp(firstInput+l,l,currDspInput==l);
    }
}

uint8_t coders_for_mapping(){

    bool mode_scope=false;
    
    uint8_t currInput=0;    // input for current cursor 
    uint8_t currDsp=0;      // line for current cursor

    coderSetup(mappingCoders,voicesSw,maxMappingCoders,3);

    fullMappingDsp(0,0);

        while(1){

            fillVoices();

            ws_show_3(30);
            ledblinkn(2);
            if(!mode_scope){test_st7789_2();}       // animation balayage de lignes
            debug_ticker();

            for(uint8_t coder=0;coder<MAPPING_CODER_NB;coder++){        // 1 codeur pour la ligne et 1 codeur pour le choix de la sortie
                int s=tst_switchs(coder,MAPPING_CODER_NB);            
                if(s>=0 || s<=-99){return s;}

                uint32_t cc=mappingCoders[coder];
                if(coder==0){                                           // coder 0 mouvemements verticaux

                        if(currInput<MAX_INPUTS && cc>currInput){               // cursor move down
                                              
                            mappingCoders[1]=in_table_srce[currInput+1];
                            if(currDsp<NB_DSP_LINES){                           // no scroll
                                mappingLineDsp(currInput,currDsp,false);        // restore prev
                                currDsp++;currInput++;
                                mappingLineDsp(currInput,currDsp,true);    
                            }
                            else {                                              // scroll down
                                fullMappingDsp(currInput++ - NB_DSP_LINES,currDsp);
                            }
                        }
                        else if(currInput>0 && cc<currInput){                   // cursor move up
                        
                            
                            mappingCoders[1]=in_table_srce[currInput-1];
                            if(currDsp>0){                                      // no scroll
                                mappingLineDsp(currInput,currDsp,false);        // restore prev
                                currDsp--;currInput--;
                                mappingLineDsp(currInput,currDsp,true);    
                            }
                            else {                                              // scroll up
                                fullMappingDsp(currInput--,currDsp);                                
                            }
                        }
                }
                if(coder==1 && cc!=in_table_srce[currInput]){                   // choix de la sortie sur l'input courante
                    // mask empty outputs
                    //if(out_table_name[])
                    in_table_srce[currInput]=cc;
                    mappingLineDsp(currInput,currDsp,true);
                }
            }
        }          
}

// ****** coders for menu ******
#define NO_VAR_CHANGE   false       // pas de modif de variables
#define VAR_CHANGE      true

void menuLineDsp(const char* menu,uint8_t line,uint8_t len,bool rev,uint8_t type,uint8_t coder,uint32_t cc,bool mode_scope,bool varChge){
        uint16_t fgc=GREEN;
        uint16_t bgc=0x0000;
        uint16_t buc=fgc;
        if(rev){fgc=bgc;bgc=buc;}
        buf[0]=line+48;
        buf[1]=' ';

        switch(type){
            case MENU0:sprintf(buf,"%2d  %s",line,menu+line*len);break; // général
            case LFOS:
                if(coder==OSCMENU){
                    setLfosFrequency(calcFreq(lfosCoders[line])/1000,line,lfosCoderCycleR[line]);
                }
                else if(coder==OSCCODERFREQ && varChge){                  
                    setLfosFrequency(calcFreq(cc)/1000,line,lfosCoderCycleR[line]);
                }
                else if(coder==OSCCODERRC && varChge){
                    setLfosFrequency(lfosFrequency[line],line,cc);
                }                
                sprintf(buf+2,"%1.3f %1.3f %d   ",lfosFrequency[line],1/lfosFrequency[line],lfosCoderCycleR[line]-MAXCODER_RC/2);
                break;  
            case VOICES:
                if(coder==OSCMENU){                
                    setVoiceFrequency(calcFreq(voices[line].coderFreq),&voices[line],voices[line].coderCycleR);
                }
                else if(coder==OSCCODERFREQ && varChge){                  
                    setVoiceFrequency(calcFreq(cc),&voices[line],voices[line].coderCycleR);
                    voices[line].coderFreq=cc;
                }
                else if(coder==OSCCODERRC && varChge){
                    setVoiceFrequency(voices[line].frequency,&voices[line],cc);
                    voices[line].coderCycleR=cc;
                }
                else if(coder==OSCGENAMP && varChge){
                    setVoiceFrequency(voices[line].frequency,&voices[line],voices[line].coderCycleR);
                    voices[line].coderGenAmpl=cc;voices[line].genAmpl=amplLevel[cc];               
                }                
                sprintf(buf+2,"%4.3f %i %u",voices[line].frequency,voices[line].coderCycleR-MAXCODER_RC/2,voices[line].coderGenAmpl);
                break;
            case ADSR:
                switch (coder){
                    case ADSRMENU: break;
                    case ADSRATT:adsrAttCoder[line]==cc;
                    case ADSRDEC:adsrDecCoder[line]==cc;
                    case ADSRSUS:adsrSusCoder[line]==cc;
                    case ADSRREL:adsrRelCoder[line]==cc;
                    case ADSRLEV:adsrLevCoder[line]==cc;
                    default: break;
                }
                sprintf(buf+2,"%u %u %u %u %u",adsrAttCoder[line],adsrDecCoder[line],adsrSusCoder[line],adsrRelCoder[line],adsrLevCoder[line]);
            default:break;
        }
        if(!mode_scope){tft_draw_text_12x12_dma_mult(0,line*(12*2+1)+begline,buf,fgc,bgc,1);}
}

void fullMenuDsp(const char* title,const char* menu,uint8_t linesNb,uint8_t line_len,uint8_t currline,uint8_t type,uint8_t coder,uint32_t cc,bool mode_scope){

    tft_fill_rect_blank(begline,0,TFT_H-begline,TFT_W);
    title_dsp(title,0,99);

    for(uint8_t l=0;l<linesNb;l++){
        menuLineDsp(menu,l,line_len,currline==l,type,coder,cc,mode_scope,NO_VAR_CHANGE);
    }
}

// ****** coders_for_menu() ****** affiche un menu avec ligne courante en reverse avec des saisies optionnelles ; 
// si les lignes ont un libellé, text pointe sur le tableau[lineNb,line_len] ; lineNb nombre de lignes du menu et line_len la longueur du libellé
// si aucune saisie/variables c'est le type 0 (cTC[0] contient la valeur courante du 1er coder et maxi le nombre de lignes à afficher-1) - voir menu0
// s'il y a des variables à afficher c'est un type!=0 : créer l'enum du type et une ligne d'affichage dans menuLineDsp
// s'il y a des variables à saisir via coder, uint16_t* var[] contient les pointeurs sur les tableaux uint16_t[line] (valeur courante du coder correspondant)
// donc var[coder-1][line] permet d'accéder à ces valeurs de coder (traitement spécifique éventuel selon le type dans menuLineDsp (ou ailleurs)
// varNb est le nombre de variables 
// les traitements associés à lamodif de variables sont appelés depuis menuLineDsp() ou l'affichage de la ligne est décrit
// switch : la sortie est déclenchée soit par le "return button" soit par l'appui du coder 0 ; la valeur retournée est le n° de ligne
// les autres switchs passent en mode scope si le type de menu le gère ; coderNb indique le nombre de coders valides (coder 0 inclu)
uint8_t coders_for_menu(const char* title,const char* text,uint8_t linesNb,uint8_t line_len,uint8_t type,volatile int16_t *cTC, volatile bool *cTS, uint16_t *maxi,uint16_t** var,uint8_t varNb,uint8_t switchsNb)
{

    uint8_t line=0;
    bool mode_scope=false;
    bool type_scope=false;
    bool firstScope=true;
    uint8_t wave=0;
    uint8_t debug=false;

    coderSetup(cTC,cTS,maxi,linesNb);   // coderSetup ignore lineNb

    fullMenuDsp(title,text,linesNb,line_len,0,type,0,0,false);

    while(1){
            
            fillVoices();
        
            ws_show_3(30);
            ledblinkn(2);
            if(!mode_scope){test_st7789_2();}    // animation balayage de lignes
            debug_ticker();          

            int s=tst_switchs_(switchsNb);            
            if(s==0 || s==-99){return line;}
            if(s>0){mode_scope=true;firstScope=true;wave=s-1;type_scope=!type_scope;}

            for(uint8_t coder=0;coder<varNb+1;coder++){       

                uint32_t cc=cTC[coder];
                
                // coder 0 : depl vertical
                if(coder==0 && cc!=line){
                                  
                    menuLineDsp(text,line,line_len,false,type,coder,cc,mode_scope,NO_VAR_CHANGE);    // enlever le rev
                    line=cc;
                    for(uint8_t k=0;k<varNb;k++){
                        if(var[k]!=nullptr){
                            cTC[k+1]=var[k][line];   // rechargement de la valeur actuelle des coder(1 à n, le 0 est pour le depl vertical) pour la nouvelle ligne ()
                        }
                    }
                    menuLineDsp(text,line,line_len,true,type,coder,cc,mode_scope,NO_VAR_CHANGE);     // mettre le rev 
                    title_dsp(title,line,1);          
                }

                // coders 1 à n update variables des enregistrements
                if(varNb>0 && coder>0 && var[coder-1]!=nullptr){        // coder 0 pour depl vertical ; (ex lfos : coder 1 freq, coder 2 rc)
                        if(var[coder-1][line]!=cc){                     // maj valeur coder et affichage changement 
                            var[coder-1][line]=cc;
                            menuLineDsp(text,line,line_len,true,type,coder,cc,mode_scope,VAR_CHANGE);  // contient les traitements associés à la modif de variables
                            title_dsp(title,line,type);
                        }
                }        
            }
            if(mode_scope){
                if(type==LFOS){     
                    scope(&lfoScopeBuffer[line*OSC_SCOPE_BUFFER_LEN],lfosFrequency[line],begline,false,firstScope,0,wave,true);firstScope=false;
                    title_dsp(title,line,LFOS);         
                }
                else if(type==VOICES){ 
                    if(firstScope){
                        //printf("i2s_buffer f:%f rc:%i ampl:%d\n",voices[0].frequency,voices[0].coderCycleR,voices[0].basicWaveAmpl[W_SINUS]);delay_ms(100);
                        //dumpStr(i2s_buffer[0],256);
                        title_dsp(title,line,VOICES);
                    }
                    if(type_scope){scope(voicesDataBuffer+line*OSC_SCOPE_BUFFER_LEN,voices[line].frequency,begline,false,firstScope,0,wave,true);}
                    //scope(i2s_buffer[0],voices[0].frequency,14,false,true,0,0,false);firstScope=false;     // scope mode_data
                    else{scope(i2s_buf_scope,voices[line].frequency,begline,false,firstScope,0,wave,false);}
                    firstScope=false;         
                }
                else mode_scope=false;
            }          
    }
}