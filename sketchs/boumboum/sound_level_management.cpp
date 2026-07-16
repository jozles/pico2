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

int32_t v0=0;
void __not_in_flash_func(setVoicesAmpl)(uint8_t v,uint8_t item,int32_t valeur){         // basicWaveAmpl est calculé avec {coderWaveAmpl , coderWaveAmplAtt , ctl_input_val et amplLevel}

    uint32_t s16=(1<<16)-1;
    uint8_t shift=8;
    
    ctl_input_val[voices[v].voice_ctl_input_id[item]]=valeur;

    if(item==BASIC_WAVES_NB){                                                           // gestion de genAmpl
        int32_t v0=(int16_t)(valeur*voices[v].coderGenAmplAtt)>>shift;
        int32_t v1=amplLevel[voices[v].coderGenAmpl]+v0;
        if(v1<0){v1=0;}
        if(v1>s16){v1=s16;}
        voices[v].genAmpl=v1;        
    }
    else                                                                                // les waves
    {
        int32_t v0=(int16_t)(valeur*voices[v].coderWaveAmplAtt[item])>>shift;           // (lfo/adsr valeur int16_t) (coderAtt 0-256) résultat int16_t 
        int32_t v1=(int32_t)amplLevel[voices[v].coderWaveAmpl[item]]+v0;                // amplLevel uint16_t ; v1 = 2*int16_t -> int32_t 
        if(v1<0){v1=0;}
        if(v1>s16){v1=s16;}
        voices[v].basicWaveAmpl[item]=v1;                                               // 0 -> (0x7fff)

if(voices[v].basicWaveAmpl[item]!=v0){
    printf("%u %u %i x:%u v:%i v0:%i val:%i\n",v,item,voices[v].basicWaveAmpl[item],voices[v].voice_ctl_input_id[item],ctl_input_val[voices[v].voice_ctl_input_id[item]],v0,valeur);
    v0=voices[v].basicWaveAmpl[item];
}
    }
}

void __not_in_flash_func(setVoicesAmpl)(uint8_t v,uint8_t item){
    setVoicesAmpl(v,item,ctl_input_val[voices[v].voice_ctl_input_id[item]]);
}
