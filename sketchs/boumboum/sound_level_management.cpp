#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "frequences.h"
#include "util.h"
#include "const.h"
#include "input_tables_management.h"
#include "sound_level_management.h"

extern Voice voices[MAX_VOICES];
extern int16_t ctl_input_val[MAX_INPUTS];

extern uint16_t    lfosCodersFreq[MAX_LFO];                  // frequency coder value
extern uint16_t    lfosCodersFreqAtt[MAX_LFO];               // frequency input attenuator value
extern uint16_t    lfosCoderCycleR[MAX_LFO];
extern int16_t     lfo_ctl_input_id[MAX_LFO][MAX_INPUTS_PER_OBJ];

uint8_t stepAmpl=MAX_16B_LINEAR_VALUE/16;     // nbre d'intervalles / 3db
uint16_t amplLevel[MAX_16B_LINEAR_VALUE];


// *************************** tables *******************************

void showAmplIncr(){
  printf("  intervalles d'amplitude\n");
  for(uint8_t i=0;i<MAX_16B_LINEAR_VALUE;i++){
      printf("%d %d\n",i,amplLevel[i]);
  }
  printf("\n");
}

void fillAmplIncr(){          // fonctionne avec stepAmpl mini 2 !!!

  amplLevel[0]=0;

  uint8_t j=1;
  uint8_t i=1;
  while(i<MAX_16B_LINEAR_VALUE){
    amplLevel[i]=(uint16_t)roundf(pow(2,((float)((int)(i/stepAmpl))+((float)j/stepAmpl))));
    j++;if(j>=stepAmpl){j=0;}
    i++;     
  }
  //showAmplIncr();
}

int32_t vx=0;
void __not_in_flash_func(setVoicesAmpl)(uint8_t v,uint8_t item,int32_t valeur){         // basicWaveAmpl est calculé avec {coderWaveAmpl , coderWaveAmplAtt , ctl_input_val et amplLevel}

    uint32_t s16=(1<<16)-1;
    uint8_t shift=MAX_CTL_ATT_SHIFT;
    
    ctl_input_val[voices[v].voice_ctl_input_id[item]]=valeur;

    if(item==BASIC_WAVES_NB){                                                           // gestion de genAmpl
        int32_t v0=(int16_t)(valeur*voices[v].coderGenAmplAtt)>>shift;
        int32_t v1=amplLevel[voices[v].coderGenAmpl]+v0;
        if(v1<0){v1=0;}
        if(v1>s16){v1=s16;}
        voices[v].genAmpl=v1;        
    }
    else                                                                                // gestion des waves
    {
        uint32_t vat=voices[v].coderWaveAmplAtt[item];
        int32_t v0=(int16_t)((valeur*vat)>>shift);           
        int32_t v1=(int32_t)amplLevel[voices[v].coderWaveAmpl[item]]+v0;                // amplLevel uint16_t ; v1 = 2*int16_t 
        if(v1<0){v1=0;}                                                                 // écrêtage
        if(v1>s16){v1=s16;}                                                             // écrêtage
        voices[v].basicWaveAmpl[item]=v1;                                               // 0 -> (0x7fff)

if(voices[v].basicWaveAmpl[item]!=vx){
    //printf("%u %u %i x:%u vat:%u v:%i v0:%i val:%i\n",v,item,voices[v].basicWaveAmpl[item],voices[v].voice_ctl_input_id[item],voices[v].coderWaveAmplAtt,ctl_input_val[voices[v].voice_ctl_input_id[item]],v0,vat);
    printf("%u %u %i %u %i %i\n",v,item,valeur,vat,v0,v1);
    vx=voices[v].basicWaveAmpl[item];
}
    }
}

void __not_in_flash_func(setVoicesAmpl)(uint8_t v,uint8_t item){
    setVoicesAmpl(v,item,ctl_input_val[voices[v].voice_ctl_input_id[item]]);
}

void __not_in_flash_func(setVoicesFreqAtt)(uint8_t v,uint32_t coderF)
{
                        voices[v].coderFreqAtt=coderF;                                           // atténuateur pour ctl_input_freq 
                        int16_t fi = ctl_input_val[voices[0].voice_ctl_input_id[VFRQ]]>>3;       // normalisation ctl_input_freq
                        uint32_t fc = voices[v].coderFreq+fi*coderF/MAX_CTL_ATT;                 // fc=coderFreq+ctl_input_freq atténué 
                        signal_overflow("vce_freq:",v,fc,VCES_MIN_FREQ_CODERS,VCES_MAX_FREQ_CODERS);
                        setVoiceFrequency(calcFreq(fc),&voices[v],voices[v].coderCycleR);   
}

void __not_in_flash_func(setLfosFrParams)(uint8_t l)
{
                        int16_t fi = ctl_input_val[lfo_ctl_input_id[l][VFRQ]]>>3;                 // normalisation ctl_input_freq
                        int16_t fc = lfosCodersFreq[l]+fi*lfosCodersFreqAtt[l]/MAX_CTL_ATT;                     // fc=coderFreq+ctl_input_freq atténué

                        signal_overflow("lfo_freq:",l,fc,LFOS_MIN_FREQ_CODERS,LFOS_MAX_FREQ_CODERS);
                        float fr=calcFreq(fc)/VOICE_FREQ_DIVIDER;
                        setLfosFrequency(fr,l,lfosCoderCycleR[l]);
                        printf("lfo#:%d cfr:%u cFrAtt:%u fi:%i fc:%i fr:%f f_id:%d \n",l,lfosCodersFreq[l],lfosCodersFreqAtt[l],fi,fc,fr,lfo_ctl_input_id[l][VFRQ]);                        
}


void __not_in_flash_func(setLfosFreqAtt)(uint8_t l,uint32_t coderF)
{
                        lfosCodersFreqAtt[l]=coderF;
                        setLfosFrParams(l);

                        /*int16_t fi = ctl_input_val[lfo_ctl_input_id[l][VFRQ]]>>3;                 // normalisation ctl_input_freq
                        int16_t fc = lfosCodersFreq[l]+fi*coderF/MAX_CTL_ATT;                     // fc=coderFreq+ctl_input_freq atténué
                        //printf("lfo#:%d cc(att):%d finp:%i fc:%i f_id:%d \n",l,coderF,fi,fc,lfo_ctl_input_id[l][VFRQ]);
                        signal_overflow("lfo_freq:",l,fc,LFOS_MIN_FREQ_CODERS,LFOS_MAX_FREQ_CODERS);
                        setLfosFrequency(calcFreq(fc)/VOICE_FREQ_DIVIDER,l,lfosCoderCycleR[l]);*/
}

void __not_in_flash_func(setLfosFreq)(uint8_t l,uint32_t coderF)
{
                        lfosCodersFreq[l]=coderF;
                        setLfosFrParams(l);
                        /*int16_t fi = ctl_input_val[lfo_ctl_input_id[l][VFRQ]]>>3;                 // normalisation ctl_input_freq 
                        uint16_t fc = coderF+fi*lfosCodersFreqAtt[l]/MAX_CTL_ATT;                 // fc=coderFreq+ctl_input_freq atténué                
                        setLfosFrequency(calcFreq(fc)/VOICE_FREQ_DIVIDER,l,lfosCoderCycleR[l]);*/    
}