#include <stdio.h>
#include "pico/stdlib.h"
#include "frequences.h"
#include "util.h"
#include "bb_i2s.h"
#include "const.h"
#include <math.h>

const uint8_t octNb = OCTNB;
float baseFreq = FREQ0;
float octFreq[octNb+1];
int32_t sineWaveform[BASIC_WAVE_TABLE_LEN];
int32_t squareWaveform[BASIC_WAVE_TABLE_LEN];
int32_t triangleWaveform[BASIC_WAVE_TABLE_LEN];
int32_t sawtoothWaveform[BASIC_WAVE_TABLE_LEN];
int32_t pinkNoiseWaveform[BASIC_WAVE_TABLE_LEN];
int32_t whiteNoiseWaveform[BASIC_WAVE_TABLE_LEN];

const uint16_t octIncrNb = 409;
float octIncr[octIncrNb];

uint8_t stepAmpl=MAX_16B_LINEAR_VALUE/16;     // nbre d'intervalles / 3db
uint16_t amplLevel[MAX_16B_LINEAR_VALUE];

extern volatile bool i2s_buf_free[];

void fillAmplIncr(){

  amplLevel[0]=0;

  uint8_t j=1;
  uint8_t i=1;
  while(i<MAX_16B_LINEAR_VALUE){
    amplLevel[i]=(uint16_t)roundf(pow(2,((float)((int)(i/stepAmpl))+((float)j/stepAmpl))));
    j++;if(j>=stepAmpl){j=0;}
    i++;     
  }
}

void showAmplIncr(){
  printf("  intervalles d'amplitude\n");
  for(uint8_t i=0;i<MAX_16B_LINEAR_VALUE;i++){
      printf("%d %d\n",i,amplLevel[i]);
  }
  printf("\n");
}

//
// Les amplitudes sont des valeurs 16 bits positives utilisées pour multiplier
// les échantillons et former des 32 bits signés pour le CODEC
// les coders d'amplitude ont un nombre d'incréments limité à MAX_16B_LINEAR_VALUE
// MAX_16B_LINEAR_VALUE / 16 (les 16 bits des valeurs d'amplitude) donne le nombre d'intervalles stepAmpl entre 2 incréments
// la valeur d'amplitude est : 2 ^ ( (n° d'incr / stepAmpl) + (1/stepAmpl) )
// soit 0 à 46341 avec stepAmpl=2 : 0=>0 1=>1 2=>2 3=>3 4=>4 5=>6 6=>8 7=>11 8=>16 9=>23 10=>32... 30=>32768 31=>46341 (si 33 incréments 32=65536)
//
// les amplitudes sont stockées en valeur d'amplitude 16 bits et en valeur linéaire de coder
//
// lorsque les amplitudes proviennent d'une balance (mode automixer), 
// l'intervalle des coders est :
// fact(nb-1)/2 (PPCM) et les valeurs 0->MAX_16B_LINEAR_VALUE-1 deviennent 0->(MAX_16B_LINEAR_VALUE-1)*fact(nb-1)/2
// pour utiliser ces amplitudes, 
// diviser la valeur par fact(nb-1)/2 pour obtenir le n° d'incrément 0->MAX_16B_LINEAR_VALUE-1
// le reste vaut 0->fact(nb-1)/2 ; une table des proportions permet de faire (delta incr)*proportion pour obtenir l'ampl exacte
// comme l'inverse est pénible à effectuer, les 2 valeurs sont stockées : 
// la valeur codeur 0->(MAX_16B_LINEAR_VALUE-1)*fact(nb-1)/2 et la valeur 16 bits de sortie
//
//
void automixer(uint8_t nb,uint16_t* ampl,uint8_t chgd){
  uint8_t stepNb=1;
  for(uint8_t i=3;i<nb-1;i++){
    stepNb*=i;
  }

}

#define N 4093
int16_t noise_table[N];

uint32_t nPhase = 0;           // Q16.16
uint32_t nStep  = 60817408;    // Q16.16

const int32_t alpha = 32113;  // 0.98 en Q15

volatile int32_t pink_state = 0;       // Q15 interne

static uint32_t seed = 0xA5C3412F;

static inline uint32_t xrnd() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

void init_noise(){
  for (int i = 0; i < N; i++)
    noise_table[i] =  (int16_t)(xrnd() >> 16);
        
        /*// option
        int32_t a = (int16_t)(xrnd() >> 16);
        int32_t b = (int16_t)(xrnd() >> 16);
        int32_t c = (int16_t)(xrnd() >> 16);
        noise_table[i] = (int16_t)((a + b + c) / 3);*/
}

static inline void get_noise(int16_t *white, int16_t *pink)
{
    // --- Bruit blanc bande limitée ---
    nPhase += nStep;
    uint32_t limit = (uint32_t)N << 16;

    // branchless wrap using subtraction and conditional negation
    uint32_t tmp = nPhase - limit;
    nPhase = tmp + ((tmp >> 31) & limit);

    *white = noise_table[nPhase>>16];

    // bruit rose 1-pôle branchless
    pink_state=(alpha * pink_state + (32768 - alpha) * (*white)) >> 15;
    *pink = (int16_t)pink_state;

}

// production des valeurs d'échantillon pour les différentes formes d'onde
void fillBasicWaveForms(){
    printf("  filling basic %d %d %d\n",(BASIC_WAVE_TABLE_LEN/4),(BASIC_WAVE_TABLE_LEN/2),BASIC_WAVE_TABLE_LEN-1);
    for(uint16_t i=0;i<BASIC_WAVE_TABLE_LEN/4;i++){
        sineWaveform[i]= (uint16_t)(sin(((float)i)/BASIC_WAVE_TABLE_LEN*2*PI)*MAX_AMP_VAL);
        sineWaveform[((BASIC_WAVE_TABLE_LEN/2)-1)-i]=sineWaveform[i];
        sineWaveform[i+(BASIC_WAVE_TABLE_LEN/2)]=-sineWaveform[i];
        sineWaveform[BASIC_WAVE_TABLE_LEN-1-i]=-sineWaveform[i];
        
        squareWaveform[i]=MAX_AMP_VAL;
        squareWaveform[i+(BASIC_WAVE_TABLE_LEN/4)]=MAX_AMP_VAL;
        squareWaveform[i+(BASIC_WAVE_TABLE_LEN/2)]=-MAX_AMP_VAL;
        squareWaveform[BASIC_WAVE_TABLE_LEN-1-i]=-MAX_AMP_VAL;

        triangleWaveform[i]=i*(MAX_AMP_VAL/(BASIC_WAVE_TABLE_LEN/4));
        triangleWaveform[((BASIC_WAVE_TABLE_LEN/2)-1)-i]=triangleWaveform[i];
        triangleWaveform[i+(BASIC_WAVE_TABLE_LEN/2)]=-triangleWaveform[i];
        triangleWaveform[BASIC_WAVE_TABLE_LEN-1-i]=-triangleWaveform[i];

        sawtoothWaveform[i]=i*(MAX_AMP_VAL/(BASIC_WAVE_TABLE_LEN/2));
        sawtoothWaveform[(BASIC_WAVE_TABLE_LEN/4)+i]=sawtoothWaveform[i]+MAX_AMP_VAL/2;
        sawtoothWaveform[(BASIC_WAVE_TABLE_LEN/2)+i]=-(MAX_AMP_VAL-sawtoothWaveform[i]);
        sawtoothWaveform[BASIC_WAVE_TABLE_LEN-1-i]=-sawtoothWaveform[i];        

    }
}

// tableau des fréquences d'octaves
void fillOctFreq() { 
  for (uint8_t i = 0; i <= octNb; i++) {
    octFreq[i] = baseFreq * (1<<i); 
  }
  //showOctFreq();  
}

void showOctFreq() 
{ 
  printf("  fréquences des octaves\n");
  for (uint16_t i = 0; i < octNb; i++ ){
    printf("%d: %5.3f - ",i,octFreq[i]);
  }
  printf("\n");
}

// tableau des ratios d'incréments sur 1 octave
void fillOctIncr() 
{ 
  for (uint16_t i = 0; i < octIncrNb; i++) {
    octIncr[i] = (float)(powf((float)2,(float)i/(float)octIncrNb))-1; 
  }
  //showOctIncr(0,1);
}                        

void showOctIncr(float oct0,float octn)
{ 
  for(uint8_t j=oct0;j<octn;j++){

    printf("  filling ratios d'incréments d'octave oct:%d f:%4.3f à f:%4.3f\n",j,octFreq[j],octFreq[j+1]);
    uint8_t dec=4;
    uint8_t step=4;
    uint16_t max=octIncrNb/step*step;
    float delta=octFreq[j+1]-octFreq[j];
  
    for (uint16_t i = 0; i < max; i+=step ){
      if(i==0 || i==max-step){
        printf("octIncr[%03d] = %0.4f=%5.3f  %0.4f=%5.3f %0.4f=%5.3f %0.4f=%5.3f\n",i,
          octIncr[i],delta*octIncr[i]+octFreq[j],
          octIncr[i+1],delta*octIncr[i+1]+octFreq[j],
          octIncr[i+2],delta*octIncr[i+2]+octFreq[j],
          octIncr[i+3],delta*octIncr[i+3]+octFreq[j]);
      }
    }
  }
}

// calcul de la fréquence sonore à partir de la valeur linéaire
float calcFreq(uint16_t val) // from lin value (0-octIncrNb*OCTNB) to snd value (baseF à baseF*2^OCTNB)
{ 
  uint8_t oct = val/ octIncrNb;
  uint16_t incr = val % octIncrNb;
  float freq = octFreq[oct] +octIncr[incr]*(octFreq[oct+1]-octFreq[oct]);
  return freq;
}

uint16_t calcCoderFreq(float freq) // from snd value to coder value
{ 
    uint8_t oct = 0;
    while (octFreq[oct+1] <= freq && oct < OCTNB)
        oct++;

    float f0 = octFreq[oct];
    float f1 = octFreq[oct+1];

    float alpha = (freq - f0) / (f1 - f0);
    if (alpha < 0) alpha = 0;
    if (alpha > 1) alpha = 1;

    uint16_t incr = (uint16_t)round(alpha * octIncrNb);
    return oct * octIncrNb + incr;
}

// initialisation des tableaux pour permettre calcFreq()
void sound_tables_init()        
{  
  printf(" sound_tables_init\n");
  
  fillOctFreq();
  fillOctIncr();
  fillBasicWaveForms();
  init_noise();
  fillAmplIncr();
}

void voiceInit(uint16_t coderF,Voice* voices)
{

    for(uint8_t v=0;v<VOICES_NB;v++){
        voices[v].maxCoderFreq=10000;
        voices[v].genAmpl=0x7fff;
        voices[v].coderFreq0=coderF;
        voices[v].coderFreq=voices[v].coderFreq0;
        float f=calcFreq(voices[v].coderFreq);          // 440Hz
        setNewFrequency(f,&voices[v]);    
        voices[v].frequency=calcFreq(voices[v].coderFreq);
        voices[v].newFrequency=voices[v].frequency;

        voices[v].sampleNbToFill=SAMPLE_BUFFER_SIZE;    
        voices[v].currentSample=0;
        voices[v].currEch=0;
        voices[v].currEchFra=0;

        voices[v].noisePhase = 0;           // Q16.16
        voices[v].noiseStep  = 60817408;    // Q16.16

        for(uint8_t i=0;i<W_NB;i++){
            voices[v].coderAmpl[i]=0;
            voices[v].coderAmpl0[i]=99;     // force basicWaveAmpl update
            voices[v].maxCoderAmpl[i]=31;
            voices[v].basicWaveAmpl[i]=0;
            voices[v].coderSw[i]=0;
        }
    }
}

void voiceInit(float freq,Voice* voices){
   voiceInit(calcCoderFreq(freq),voices);
}   

// update voice[].newFrequency - compute newSteps
void setNewFrequency(float freq,Voice* v){
    
    v->newFrequency=freq;

    float k=(uint32_t)BASIC_WAVE_TABLE_LEN*v->newFrequency/SAMPLE_RATE;
    v->newStepInt=(uint32_t)k;
    v->newStepFra=(uint32_t)((k-v->newStepInt)*MAX_STEP_FRA);
}

uint16_t getAmpl(Voice* v,uint8_t wav){
  return amplLevel[v->coderAmpl[wav]];
}

void fillVoiceBuffer(int32_t* vBuffer,Voice* v,uint8_t what,uint8_t bufNum){   // 5.8mS pour les 6 sources @512 samples (23mS@44100Hz)
gpio_put(TST_PIN,HIGH);

    i2s_buf_free[bufNum]=false;

    uint32_t currEch      = v->currEch;
    uint32_t currEchFra   = v->currEchFra;
    uint32_t stepInt      = v->stepInt;
    uint32_t stepFra      = v->stepFra;
    nPhase       = v->noisePhase;
    nStep        = v->noiseStep;
    uint32_t limit = (uint32_t)N << 16;
    int32_t  genAmpl      = v->genAmpl;
    int32_t  waveAmplSin  = v->basicWaveAmpl[W_SINUS];
    int32_t  waveAmplTri  = v->basicWaveAmpl[W_TRIANGLE];
    int32_t  waveAmplSaw  = v->basicWaveAmpl[W_SAWTOOTH];        
    int32_t  waveAmplSqr  = v->basicWaveAmpl[W_SQUARE];    
    int32_t  waveAmplWhi  = v->basicWaveAmpl[W_WHITE_NOISE];
    int32_t  waveAmplPin  = v->basicWaveAmpl[W_PINK_NOISE];
    int32_t* voiceBuffer=&vBuffer[0];

    if(v->newFrequency!=0){
      stepInt=v->newStepInt;
      v->stepInt=stepInt;
      stepFra=v->newStepFra;
      v->stepFra=stepFra;
      v->frequency=v->newFrequency;
      v->newFrequency=0;
    } 
    
    for(uint32_t s = 0; s < v->sampleNbToFill; s++)
    {

        currEchFra += stepFra;
        uint32_t carry = (currEchFra > MAX_STEP_FRA);
        currEchFra -= carry * MAX_STEP_FRA;
        currEch += stepInt + carry;
        currEch -= (currEch >= BASIC_WAVE_TABLE_LEN) * BASIC_WAVE_TABLE_LEN;

        int32_t pre=sineWaveform[currEch] * waveAmplSin;
        pre += triangleWaveform[currEch] * waveAmplTri;
        pre += sawtoothWaveform[currEch]* waveAmplSaw;
        pre += squareWaveform[currEch]* waveAmplSqr;

        // noises

        nPhase += nStep;

        // branchless wrap using subtraction and conditional negation
        uint32_t tmp = nPhase - limit;
        nPhase = tmp + ((tmp >> 31) & limit);

        //if (nPhase >= limit) nPhase -= limit;

        int32_t white = noise_table[nPhase>>16];

        pre += white * waveAmplWhi;

        // bruit rose 1-pôle branchless
        pink_state=(alpha * pink_state + (32768 - alpha) * (white)) >> 15;
          
        pre += (int16_t)pink_state * waveAmplPin;

        *voiceBuffer=pre;
        voiceBuffer++;
        *voiceBuffer=pre;
        voiceBuffer++;
    }

    v->currEch    = currEch;
    v->currEchFra = currEchFra;
    v->noisePhase = nPhase;   

//dumpStr(vBuffer,256);
gpio_put(TST_PIN,LOW);    
}

