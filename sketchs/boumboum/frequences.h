#ifndef _FREQUENCES_H_
#define _FREQUENCES_H_ 

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

struct Voice {
    uint16_t    sampleNbToFill;                 // sample Nb for 1 period    
    uint32_t    currentSample;                  // last value pushed in i2s buffer                  
    uint16_t    stepInt;                        // partie entière du step montant
    uint32_t    stepFra;                        // partie fractionnaire du step montant
    //uint16_t    stepIntD;                       // partie entière du step descendant
    //uint32_t    stepFraD;                       // partie fractionnaire du step descendant    
    uint16_t    currEch;                        // dernier N° d'ech utilisé
    uint32_t    currEchFra;                     // dernière valeur fractionnaire de n° d'ech calculée  
    uint32_t    noisePhase;                     // Q16.16
    uint32_t    noiseStep;                      // Q16.16                 
    volatile uint16_t    basicWaveAmpl[BASIC_WAVES_NB];  // ampl value for coderAmpl value
    volatile int16_t     coderAmpl[BASIC_WAVES_NB];      // last coder value for ampl
    uint16_t    maxCoderAmpl[BASIC_WAVES_NB];   // max value for coderAmpl
    volatile uint16_t    genAmpl;               // ampl value for global voice
    volatile int16_t     coderGenAmpl;
    uint16_t    maxCoderGenAmpl;                // max value for coderGenAmpl
    bool        coderSw[CODER_NB];              // last Switch
    uint16_t    soundsCc[CODER_BANK_NB];
    uint16_t    adsrlCc[CODER_BANK_NB];
    float       frequency;                      // current freq
    int16_t     coderFreq;                      // last coder value for freq
    int16_t     coderFreqAtt;                   // attenuateur 0-7fff pour ctl_input freq
    uint16_t    maxCoderFreq;                   // pmax value for coderFreq
    uint8_t     coderCycleR;                    // rapport cyclique -64/+64 pour coder ; one value for all waveforms
    uint8_t     cycleR;                         // somme coderCycleR et ctl_input_val
    int16_t     coderCycleRAtt;                 // attenuateur 0-7fff pour ctl_input cra
    uint8_t     maxCoderCycleR;
    int16_t     voice_ctl_input_id[MAX_INPUTS_PER_OBJ];

};


void sound_tables_init();
void voicesInit(Voice* v,float freq,uint8_t cga);
void voicesInit(Voice* v,uint16_t coderF,uint8_t cga);
void dumpVoices(Voice* v);
void fillVoiceBuffer(int32_t* sampleBuffer,Voice* v,uint8_t bufNum);
void fillVoices();
void setVoiceFrequency(float freq,Voice* v,int8_t rc);
float calcFreq(uint16_t val);
uint16_t calcCoderFreq(float freq);
uint16_t getAmpl(Voice* v,uint8_t wav);
void setLfosFrequency(float freq,uint8_t l,int8_t rc);
void lfosHandler();
void lfosInit();

#endif  //_FREQUENCES_H_