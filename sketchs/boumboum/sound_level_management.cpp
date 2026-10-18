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
        amplLevel[i]=(uint16_t)roundf(32767.0f*powf(2.0f,(float)(i-(MAX_16B_LINEAR_VALUE-1))/stepAmpl));   // top step = 0x7fff, 3 dB steps below
        j++;if(j>=stepAmpl){j=0;}
        i++;     
    }

    assert(amplLevel[MAX_16B_LINEAR_VALUE-1] <= 32767);
  //showAmplIncr();
}

// ***************** glide of the level set by the coder *****************
// A coder step is a sudden jump of 3 dB: even with the 11.6 ms ramp of the renderer it leaves a faint crackle on a pure tone.
// The level given by the coder therefore glides (two one-pole low-passes in cascade, updated every ms, about 50 ms from 10 % to 90 %).
// The modulation (LFO, ADSR) is added after the glide and stays fast.

#define GLIDE_SHIFT     5               // time constant of each pole: 2^4 = 16 ms
#define GLIDE_START_MS  1500            // no glide during the start-up: the demo and the presets must sound at once

static int32_t glideS1[MAX_VOICES][BASIC_WAVES_NB+1];       // Q16, first pole
static int32_t glideS2[MAX_VOICES][BASIC_WAVES_NB+1];       // Q16, second pole
static int16_t glideBase[MAX_VOICES][BASIC_WAVES_NB+1];     // smoothed level used by setVoicesAmpl()
static bool    glideEnabled=false;
static uint16_t glideTicks=0;

static inline uint16_t coderLevelOf(uint8_t v,uint8_t item){                           // coder value of a wave (item 0..5) or of the voice gain (item 6)
    const uint16_t c=(item==BASIC_WAVES_NB)?voices[v].coderGenAmpl:voices[v].coderWaveAmpl[item];
    return (c<MAX_16B_LINEAR_VALUE)?c:(MAX_16B_LINEAR_VALUE-1);
}

static inline int32_t baseLevel(uint8_t v,uint8_t item){
    return glideEnabled?(int32_t)glideBase[v][item]:(int32_t)amplLevel[coderLevelOf(v,item)];
}

void __not_in_flash_func(setVoicesAmpl)(uint8_t v,uint8_t item,int16_t valeur){         // basicWaveAmpl est calculé avec {coderWaveAmpl , coderWaveAmplAtt , ctl_input_val et amplLevel}

    const int32_t amplMax=32767;                                                        // waves and genAmpl are both int16_t (0..0x7fff)
    const uint8_t shift=MAX_CTL_ATT_SHIFT;
    const int16_t inId=voices[v].voice_ctl_input_id[VSPW+item];                         // the amplitude inputs are laid out from VSPW on: VSPW + wave number (6 = gain)

    ctl_input_val[inId]=valeur;

    if(item==BASIC_WAVES_NB){                                                           // gestion de genAmpl
        int32_t v0=(valeur*voices[v].coderGenAmplAtt)>>shift;                           // multiply, then shift: the old cast to int16_t truncated the product before the shift
        int32_t v1=baseLevel(v,item)+v0;
        if(v1<0){v1=0;}
        if(v1>amplMax){v1=amplMax;}
        voices[v].genAmpl=(int16_t)v1;                                                  // clamped above
    }
    else                                                                                // gestion des waves
    {
        int32_t vat=voices[v].coderWaveAmplAtt[item];
        int32_t v0=(valeur*vat)>>shift;                                                 // signed multiply and arithmetic shift, no overflow while vat <= MAX_CTL_ATT
        int32_t v1=baseLevel(v,item)+v0;
        if(v1<0){v1=0;}                                                                 // clipping
        if(v1>amplMax){v1=amplMax;}

        voices[v].waveAmplChge[item]=true;
        voices[v].newBasicWaveAmpl[item]=(int16_t)v1;                                   // 0 -> (0x7fff), already clamped above
    }
}

void __not_in_flash_func(setVoicesAmpl)(uint8_t v,uint8_t item){                        // menu entry: recompute with the value stored for the input
    setVoicesAmpl(v,item,ctl_input_val[voices[v].voice_ctl_input_id[VSPW+item]]);
}

void __not_in_flash_func(ampGlideTick)(){                                               // called every ms by the coder timer interrupt
    if(!glideEnabled){
        if(++glideTicks>=GLIDE_START_MS){                                               // end of the start-up: the glide starts from the current coder levels
            for(uint8_t v=0;v<MAX_VOICES;v++){
                for(uint8_t item=0;item<=BASIC_WAVES_NB;item++){
                    const int16_t lev=(int16_t)amplLevel[coderLevelOf(v,item)];
                    glideS1[v][item]=glideS2[v][item]=((int32_t)lev)<<16;
                    glideBase[v][item]=lev;
                }
            }
            glideEnabled=true;
        }
        return;
    }
    for(uint8_t v=0;v<MAX_VOICES;v++){
        for(uint8_t item=0;item<=BASIC_WAVES_NB;item++){
            const int32_t tgt=((int32_t)amplLevel[coderLevelOf(v,item)])<<16;
            int32_t s1=glideS1[v][item];
            int32_t s2=glideS2[v][item];
            s1+=(tgt-s1)>>GLIDE_SHIFT;
            s2+=(s1-s2)>>GLIDE_SHIFT;
            glideS1[v][item]=s1;
            glideS2[v][item]=s2;
            const int16_t b=(int16_t)((s2+0x8000)>>16);                                 // rounded to the nearest integer
            if(b!=glideBase[v][item]){
                glideBase[v][item]=b;
                setVoicesAmpl(v,item,ctl_input_val[voices[v].voice_ctl_input_id[VSPW+item]]);   // new target with the new base level
            }
        }
    }
}