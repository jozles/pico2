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
void setVoicesAmpl(uint8_t v,uint8_t item){         // basicWaveAmpl est calculé avec {coderWaveAmpl , coderWaveAmplAtt , ctl_input_val et amplLevel}
    uint8_t shift= (sizeof(ctl_input_val[0])*8) - (32 - __builtin_clz(MAX_16B_LINEAR_VALUE-1));         // pour recadrage de val sur la taille de amplLevel
    
    if(item==BASIC_WAVES_NB){
        voices[v].genAmpl=
        amplLevel[voices[v].coderGenAmpl/MAX_16B_LINEAR_VALUE]+amplLevel[((ctl_input_val[voices[v].voice_ctl_input_id[item]])>>shift)*voices[v].coderGenAmplAtt];
    }
    else {
        voices[v].basicWaveAmpl[item]=
        amplLevel[voices[v].coderWaveAmpl[item]]+amplLevel[((ctl_input_val[voices[v].voice_ctl_input_id[item]])>>shift)]*voices[v].coderWaveAmplAtt[item]; 
if(voices[v].basicWaveAmpl[item]!=v0){
    printf("%u %u %i x:%u v:%i\n",v,item,voices[v].basicWaveAmpl[item],voices[v].voice_ctl_input_id[item],ctl_input_val[voices[v].voice_ctl_input_id[item]]);
    v0=voices[v].basicWaveAmpl[item];
}
    }
}


