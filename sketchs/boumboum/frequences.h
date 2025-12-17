#ifndef _FREQUENCES_H_
#define _FREQUENCES_H_ 


/* générateur de fréquences discrètes à partir de valeurs linéaires

  Le dac fournit des valeurs v comprises entre 0 et 2^n  (n=nombre de bits du dac)
  
  On décide du nombre d'octaves couvert OCTNB
  Il y aura (2^n)%OCTNB incréments par octave.
  La fréquence à chaque incrément est calculée par la formule :
    freq = 2^v soit 2^(int(v/OCTNB))+2^((v%OCTNB)/(n%OCTNB)) 
  on a donc 2 tables : les fréquences d'octaves et les ratios d'incréments 
  ainsi le calcul est minimisé 
  Quand on dispose d'une grande mémoire et que l'on est très pressé on peut faire une table avec les 2^n valeurs 

*/


#define OCTNB 11
#define INCRNB 409    // int(4096/10)=409
#define FREQ0 16.345  // pour avoir un LA à 440Hz avecc 409 incréments par octave  

struct voice {
    //int32_t*    sampleBuffer;
    uint16_t    sampleNbToFill;    
    uint32_t    currentSample;
    float       frequency;    
    uint16_t    amplitude;
    uint8_t     freqCoeff;
    uint32_t    dhexFreq;
    uint32_t    moduloMask;
    uint8_t     moduloShift;      
};

void fillSineWaveForms();
void fillOctFreq();
void showOctFreq(); 
void fillOctIncr();
void showOctIncr(float octF,float octF1);
float calcFreq(uint16_t val);
void freq_start();
void getEch(float freq,uint32_t sampleCounter,uint16_t sampleNbToFill,uint32_t* sampleBuffer);

void voiceInit(float freq,struct voice* v);
void fillVoiceBuffer(int32_t* sampleBuffer,struct voice* v);

#endif  //_FREQUENCES_H_