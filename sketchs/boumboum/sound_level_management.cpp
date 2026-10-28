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
#include "hardware/sync.h"

extern Voice voices[MAX_VOICES];
extern int16_t ctl_input_val[TOTAL_INPUTS_ID_REAL];

extern uint16_t    lfosCodersFreq[MAX_LFO];                  // frequency coder value
extern uint16_t    lfosCodersFreqAtt[MAX_LFO];               // frequency input attenuator value
extern uint16_t    lfosCoderCycleR[MAX_LFO];
extern int16_t     lfo_ctl_input_id[MAX_LFO][TOTAL_INPUTS_ID_REAL];

uint8_t stepAmpl=MAX_16B_LINEAR_VALUE/16;     // nbre d'intervalles / 3db
uint16_t amplLevel[MAX_16B_LINEAR_VALUE];     // old law, 3 dB steps: voice gain (an integer multiplier: 1 at step 1) and any other user of the table
uint16_t waveLevel[WAVE_LEVEL_STEPS];         // new law of the wave amplitudes: WAVE_LEVEL_STEPS fine steps over AMPL_RANGE_DB

#define AMPL_RANGE_DB   69.0f                 // range of the wave coders: step 1 is AMPL_RANGE_DB below the top step (0x7fff); lower it if step 1 is too quiet


// *************************** tables *******************************

void showAmplIncr(){
  printf("  intervalles d'amplitude (gain, loi a 3 dB)\n");
  for(uint8_t i=0;i<MAX_16B_LINEAR_VALUE;i++){
      printf("%d %d\n",i,amplLevel[i]);
  }
  printf("  niveaux des ondes (%d pas sur %.0f dB)\n",WAVE_LEVEL_STEPS,AMPL_RANGE_DB);
  for(uint8_t i=0;i<WAVE_LEVEL_STEPS;i++){
      printf("%d %d\n",i,waveLevel[i]);
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

    waveLevel[0]=0;                                                                                 // wave amplitudes: finer steps, and a range that stays audible down to step 1
    for(uint8_t k=1;k<WAVE_LEVEL_STEPS;k++){
        const float db=-AMPL_RANGE_DB*(float)(WAVE_LEVEL_STEPS-1-k)/(float)(WAVE_LEVEL_STEPS-2);    // 0 dB at the top step, -AMPL_RANGE_DB at step 1
        waveLevel[k]=(uint16_t)roundf(32767.0f*powf(10.0f,db/20.0f));
    }

    assert(waveLevel[WAVE_LEVEL_STEPS-1] <= 32767);
  //showAmplIncr();
}
// ***************** glide of the level set by the coder *****************
// A coder step is a sudden jump of the level: even with the 11.6 ms ramp of the renderer it leaves a faint crackle on a pure tone.
// The level given by the coder therefore glides (two one-pole low-passes in cascade, one step per ms, about 50 ms from 10 % to 90 %).
// The modulation (LFO, ADSR) is added after the glide and stays fast.

#define GLIDE_TAU_MS    16                                    // time constant of each pole
#define GLIDE_K         (65536/(GLIDE_TAU_MS+1))              // coefficient per ms, Q16
#define GLIDE_TICK_US   1000                                  // the glide advances by one step every ms, whatever the rate of the caller
#define GLIDE_START_MS  1500                                  // no glide during the start-up: the demo and the presets must sound at once

static int32_t  glideS1[MAX_VOICES][BASIC_WAVES_NB+1];        // Q16, first pole
static int32_t  glideS2[MAX_VOICES][BASIC_WAVES_NB+1];        // Q16, second pole
static int16_t  glideBase[MAX_VOICES][BASIC_WAVES_NB+1];      // smoothed level used by setVoiceAmpl()
static bool     glideEnabled=false;
static uint16_t glideTicks=0;

uint16_t voiceAmplCoder(const Voice* v,uint8_t item){                                   // coder of a wave (item 0..5) or of the voice gain (item 6)
    return (item==BASIC_WAVES_NB)?(uint16_t)v->coderGenAmpl:(uint16_t)v->coderWaveAmpl[item];
}

uint16_t voiceAmplAtt(const Voice* v,uint8_t item){                                     // attenuator of the same
    return (item==BASIC_WAVES_NB)?(uint16_t)v->coderGenAmplAtt:(uint16_t)v->coderWaveAmplAtt[item];
}

static inline int32_t levelOf(const Voice* v,uint8_t item){                             // level given by the coder: a wave (fine law) or the voice gain (3 dB law)
    const uint16_t c=voiceAmplCoder(v,item);
    if(item==BASIC_WAVES_NB){return amplLevel[(c<MAX_16B_LINEAR_VALUE)?c:(MAX_16B_LINEAR_VALUE-1)];}
    return waveLevel[(c<WAVE_LEVEL_STEPS)?c:(WAVE_LEVEL_STEPS-1)];
}

static inline int32_t baseLevel(const Voice* v,uint8_t item){
    return glideEnabled?(int32_t)glideBase[v-voices][item]:levelOf(v,item);
}

// compute: the target level of a wave (item 0..5) or of the voice gain (item 6), from the stored coders and the stored value of its input
void __not_in_flash_func(setVoiceAmpl)(Voice* v,uint8_t item){                          // fitted for waves & genAmpl gain control !!

    const int32_t amplMax=32767;                                                        // waves and genAmpl are both int16_t (0..0x7fff)
    const uint8_t shift=MAX_CTL_ATT_SHIFT;
    const int32_t valeur=ctl_input_val[v->voice_ctl_input_id[VSPW+item]];               // the amplitude inputs are laid out from VSPW on: VSPW + wave number (6 = gain)

    if(item==BASIC_WAVES_NB){                                                           // gain of the voice
        int32_t v0=(valeur*v->coderGenAmplAtt)>>shift;                                  // multiply, then shift: the old cast to int16_t truncated the product before the shift
        int32_t v1=baseLevel(v,item)+v0;
        if(v1<0){v1=0;}
        if(v1>amplMax){v1=amplMax;}
        v->genAmpl=(int16_t)v1;                                                         // clamped above
    }
    else if(item<BASIC_WAVES_NB)                                                        // waves
    {
        int32_t vat=v->coderWaveAmplAtt[item];
        int32_t v0=(valeur*vat)>>shift;                                                 // signed multiply and arithmetic shift, no overflow while vat <= MAX_CTL_ATT
        int32_t v1=baseLevel(v,item)+v0;
        if(v1<0){v1=0;}                                                                 // clipping
        if(v1>amplMax){v1=amplMax;}

        //v->waveAmplChge[item]=true;
        v->newBasicWaveAmpl[item]=(int16_t)v1;                                          // 0 -> (0x7fff), already clamped above
    }
}

// load the coders of a wave (item 0..5) or of the voice gain (item 6), then compute the level (same model as setVoiceFilter)
void __not_in_flash_func(setVoiceAmplCoders)(Voice* v,uint8_t item,uint16_t coder,uint16_t coderAtt){   // fitted for waves & genAmpl gain control coders !!

    const uint32_t irq=save_and_disable_interrupts();                                   // the ADSR/LFO interrupt computes the same level: no interleaving
    if(item==BASIC_WAVES_NB){
        v->coderGenAmpl=(int16_t)coder;
        v->coderGenAmplAtt=(int16_t)coderAtt;
    }
    else if(item<BASIC_WAVES_NB){
        v->coderWaveAmpl[item]=coder;
        v->coderWaveAmplAtt[item]=coderAtt;
    }
    setVoiceAmpl(v,item);
    restore_interrupts(irq);
}

static void __not_in_flash_func(glideStep)(){                                           // one millisecond of glide
    if(!glideEnabled){
        if(++glideTicks>=GLIDE_START_MS){                                               // end of the start-up: the glide starts from the current coder levels
            for(uint8_t v=0;v<MAX_VOICES;v++){
                for(uint8_t item=0;item<=BASIC_WAVES_NB;item++){
                    const int32_t lev=levelOf(&voices[v],item);
                    glideS1[v][item]=glideS2[v][item]=lev<<16;
                    glideBase[v][item]=(int16_t)lev;
                }
            }
            glideEnabled=true;
        }
        return;
    }
    for(uint8_t v=0;v<MAX_VOICES;v++){
        for(uint8_t item=0;item<=BASIC_WAVES_NB;item++){
            const int32_t tgt=levelOf(&voices[v],item)<<16;
            int32_t s1=glideS1[v][item];
            int32_t s2=glideS2[v][item];
            s1+=(int32_t)(((int64_t)(tgt-s1)*GLIDE_K)>>16);
            s2+=(int32_t)(((int64_t)(s1-s2)*GLIDE_K)>>16);
            glideS1[v][item]=s1;
            glideS2[v][item]=s2;
            const int16_t b=(int16_t)((s2+0x8000)>>16);                                 // rounded to the nearest integer
            if(b!=glideBase[v][item]){
                glideBase[v][item]=b;
                setVoiceAmpl(&voices[v],item);                                          // new target with the new base level
            }
        }
    }
}

void __not_in_flash_func(ampGlideTick)(){                                               // call it from any interrupt running at 1 kHz or faster
    static uint32_t last=0;
    static bool     started=false;
    const uint32_t  now=time_us_32();
    if(!started){started=true;last=now;return;}
    uint32_t n=(now-last)/GLIDE_TICK_US;                                                // whole milliseconds elapsed since the last step
    if(n==0){return;}
    if(n>8){n=8;last=now;}                                                              // after a long pause: do not catch up
    else{last+=n*GLIDE_TICK_US;}
    while(n--){glideStep();}
}

