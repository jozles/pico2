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
#define BASIC_WAVES_NB 6 // sinus, carré, triangle, dent de scie, bruit blanc,bruit rose 
#define WAVE_SINUS 0
#define WAVE_SQUARE 1
#define WAVE_TRIANGLE 2
#define WAVE_SAWTOOTH 3 
#define WAVE_WHITENOISE 4
#define WAVE_PINKNOISE 5
#define MAX_AMP_VAL 32767

struct Voice {
    //int32_t*    sampleBuffer;
    uint16_t    sampleNbToFill;    
    uint32_t    currentSample;
    float       frequency;
    float       newFrequency;    
    uint16_t    basicWaveAmpl[BASIC_WAVES_NB];
    uint16_t    genAmpl;
    uint8_t     freqCoeff;
    uint32_t    dhexFreq;
    uint32_t    moduloMask;
    uint8_t     moduloShift;
    uint16_t    soundsCc[CODER_BANK_NB];
    uint16_t    adsrlCc[CODER_BANK_NB];
    uint16_t    frequencyCc;
};

void fillAmplIncr(float* amplIncr);
void fillBasicWaveForms();
void fillOctFreq();
void showOctFreq(); 
void fillOctIncr();
void showOctIncr(float octF,float octF1);
float calcFreq(uint16_t val);
void freq_start();
void getEch(float freq,uint32_t sampleCounter,uint16_t sampleNbToFill,uint32_t* sampleBuffer);

void voiceInit(float freq,Voice* v);
void fillVoiceBuffer(int32_t* sampleBuffer,Voice* v);

#endif  //_FREQUENCES_H_