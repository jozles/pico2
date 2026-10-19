#ifndef _FREQUENCES_H_
#define _FREQUENCES_H_ 

#include <stdint.h>
#include "const.h"

/* générateur de fréquences discrètes à partir de valeurs linéaires

  Le dac fournit des valeurs v comprises entre 0 et 2^n  (n=nombre de bits du dac)
  
  On décide du nombre d'octaves couvert OCTNB
  Il y aura (2^n)%OCTNB incréments par octave.
  La fréquence à chaque incrément est calculée par la formule :
    freq = 2^v soit 2^(int(v/OCTNB))+2^((v%OCTNB)/(n%OCTNB)) 
  on a donc 2 tables : les fréquences d'octaves et les ratios d'incréments 
  ainsi le calcul est minimisé 
  Quand on dispose d'une grande mémoire et que l'on est très pressé on peut faire une table avec les 2^n valeurs 


  Les données des différentes voix sont dans le tableau Voice voices[VOICES_NB]
  qui contient les paramètres et les valeurs courantes de chaque voix

*/


#define OCTNB 11
#define INCRNB 409    // int(4096/10)=409
#define FREQ0 16.345  // pour avoir un LA à 440Hz avecc 409 incréments par octave 

// lfos: Q16.16 phase, 11 bits of table index + 16 fractional bits
#define LFO_PHASE_SHIFT 16
#define LFO_PHASE_MASK  ((BASIC_WAVE_TABLE_LEN << LFO_PHASE_SHIFT) - 1)
static_assert(MAX_STEP_FRA == (1u << LFO_PHASE_SHIFT), "lfosPhaseStep is built with MAX_STEP_FRA: it must match LFO_PHASE_SHIFT");

// voices: table index in the top 11 bits of a 32-bit word, so the overflow of the word acts as the mask
#define PHASE_SHIFT (32 - BASIC_WAVE_TABLE_POW)

//#define FILTER_STEPS_PER_OCT 32
#define FILTER_MAX_OCT       10
//#define FILTER_TABLE_LEN     (FILTER_MAX_OCT*FILTER_STEPS_PER_OCT + 1)

//pour ctlCombine
#define FREQ_CODE_MAX (OCTNB*INCRNB-1)                // 4498: calcFreq() reads octFreq[oct+1], oct <= 10
#define FILTER_CODE_MAX  (FILTER_MAX_OCT*INCRNB)      // 4090
static_assert(VCES_MAX_FREQ_CODERS == FREQ_CODE_MAX, "voice frequency coder maximum and FREQ_CODE_MAX must match");

#define FILTER_MAX_STAGES 4          // allocated size of the state array; the render loop currently always uses 4 stages

struct VoiceFilter {
    int32_t z[FILTER_MAX_STAGES];    // per-stage state, updated sample by sample (one value per stage, as phase is for the oscillator)
};


struct Voice {
    //uint16_t    sampleNbToFill;                 // sample Nb for 1 period    
    //uint32_t    currentSample;                  // last value pushed in i2s buffer                  
    volatile uint32_t phaseStep;        // table steps per sample, 11.21 fixed point, written and read as one word
    uint32_t          phase;            // current phase, same format: the table index is phase >> PHASE_SHIFT 
    uint32_t    noiseSeed;                     // Q16.16
    //uint32_t    noiseStep;                      // Q16.16

    volatile int16_t    genAmpl;               // genAmpl value
    volatile int16_t    coderGenAmpl;          // genAmpl coder value
    volatile uint16_t   coderGenAmplAtt;       // genAmpl input attenuator value    
    
    volatile int16_t    basicWaveAmpl[BASIC_WAVES_NB];     // wave ampl value
    volatile int16_t    newBasicWaveAmpl[BASIC_WAVES_NB];
    //volatile bool       waveAmplChge[BASIC_WAVES_NB];
    //volatile int32_t    diffWaveAmpl[BASIC_WAVES_NB];
    volatile uint16_t   coderWaveAmpl[BASIC_WAVES_NB];     // wave ampl coder value
    volatile uint16_t   coderWaveAmplAtt[BASIC_WAVES_NB];           // wave ampl input attenuator value
    
    //uint16_t    soundsCc[CODER_BANK_NB];
    //uint16_t    adsrlCc[CODER_BANK_NB];
    
    volatile float      frequency;             // current frequency
    volatile int16_t    coderFreq;             // frequency coder value
    volatile int16_t    coderFreqAtt;          // frequency input attenuator value (0-MAX_CTL_ATT)

    volatile float      filterFrequency;       // current frequency
    volatile int16_t    coderFilterFreq;       // filterFrequency coder value
    volatile int16_t    coderFilterFreqAtt;    // frequency input attenuator value (0->MAX_CTL_ATT)
    volatile int16_t    coderFilterLevAtt;     // filter input level attenuator, (0->MAX_CTL_ATT ; MAX_CTL_ATT = unity gain).
    volatile int16_t    coderFilterRes;        // filter manual res adjust (0->MAX_CTL_ATT)
    volatile int16_t    coderFilterResAtt;     // filter manual res input attenuator value (0->MAX_CTL_ATT)

    // --- filtre : fonctionnement réel, lu/écrit par fillVoiceBuffer_mono ---
    VoiceFilter filter;               // per-stage state (z[]), persists across buffers
    volatile int16_t    filterCode;           // final cutoff code 0..FILTER_CODE_MAX (coder + modulation), read once per sub-block by fillVoiceBuffer_mono
    volatile uint8_t    filterRes;            // final resonance command 0..FILTER_RES_MAX
    volatile int16_t filterCodeTarget;        // cutoff asked by the coders and the modulation ; filterCode glides towards it

    volatile uint8_t    cycleR;       // cyclic ratio value (somme coderCycleR et ctl_input_val)
    volatile uint8_t    coderCycleR;  // cyclic ratio -64/+64 coder value
    int16_t     coderCycleRAtt;       // cyclic ratio input attenuator value (0-MAX_CTL_ATT)

    int32_t     sqrTh;
    
    int16_t     voice_ctl_input_id[MAX_INPUTS_PER_OBJ];

};


void sound_tables_init();
void voicesInit(Voice* v,float freq,uint8_t cga);
void voicesInit(Voice* v,uint16_t coderF,uint8_t cga);
void dumpVoices(Voice* v,uint8_t n);
void fillVoiceBuffer(int32_t* sampleBuffer,Voice* v,uint8_t bufNum);
void fillVoices();
void envLogDump();                                          // debug: prints the envelope log when an envelope has just ended
// setVoiceXxxCoders(Voice*, coders...) loads the coders then computes the internal values (same model as setVoiceFilter)
// setVoiceXxx(Voice*)                  computes only, from the stored coders and the stored value of the input
void setVoiceFreqCoders(Voice* v, uint16_t coderFreq, uint16_t coderFreqAtt);
void setVoiceFrequency(Voice* v);
void setVoiceCycleRCoders(Voice* v, uint8_t coderCycleR, int16_t coderCycleRAtt);
void setVoiceCycleR(Voice* v);
void setVoiceSteps(float freq,Voice* v);
float calcFreq(uint16_t val);
uint16_t calcCoderFreq(float freq);

void filtersInit(uint8_t v);
float calcFilterFreq(int32_t code);
void setVoiceFilter(Voice* v, int16_t coderFilterF, int16_t coderFilterFAtt, int16_t coderFilterLevAtt, int16_t coderFilterRes,int16_t coderFilterResAtt);
void setVoiceFilterFrequency(Voice* v);
void setVoiceFilterRes(Voice* v);
void filterGlideTick();                                     // every ms (interrupt): glide of the filter cutoff
void filterGlideReport();                                   // main loop: prints the time constant when the scan changes it

void setLfosSteps(float freq,uint8_t l);
void setLfosFrCoders(uint8_t lfo, uint16_t coderFreq, uint16_t coderAtt);
void setLfosFrequency(uint8_t lfo);
void setLfosCycleR(uint8_t lfo, uint16_t coderCycleR, uint16_t coderCycleRAtt);
void lfosHandler();
void lfosInit();

int32_t ctlCombine(int32_t coder,int32_t att,int16_t input,uint8_t inShift,int32_t lo,int32_t hi);

#endif  //_FREQUENCES_H_