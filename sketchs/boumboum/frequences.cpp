#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "frequences.h"
#include "util.h"
#include "bb_i2s.h"
#include "const.h"

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

extern uint32_t millisCounter;

// current lfo values (lfoHandler triger'd by pwmIrqHandler)
float       lfosFrequency[LFOS_NB];                  // current lfo freq
int16_t     coderLfos[LFOS_NB];                      // last coder value for freq
uint16_t    lfosMaxCoderFreq[LFOS_NB];               // pmax value for lfo coderFreq
uint16_t    lfosStepInt[LFOS_NB];                    // partie entière du step lfo
uint32_t    lfosStepFra[LFOS_NB];                    // partie fractionnaire du step lfo
uint16_t    currLfoEch[LFOS_NB];
uint32_t    currLfoEchFra[LFOS_NB];
uint16_t    sineLfo[LFOS_NB];
uint16_t    squareLfo[LFOS_NB];
uint16_t    triangleLfo[LFOS_NB];
uint16_t    sawtoothLfo[LFOS_NB];
uint32_t    lfoTime=millisCounter;
uint32_t    lfoTimingInterval=1000/LFOS_SAMPLE_RATE;
int32_t     lfoScopeBuffer[LFOS_NB*LFOS_SCOPE_BUFFER_LEN];  // n° echantillons 
uint16_t    lfoScopeBufPtr=0;

// i2s

Voice voices[VOICES_NB];

extern volatile bool i2s_buf_free[];
extern int32_t* i2s_buffer[];
int32_t* i2s_buf_scope;       // last loaded buffer for scope

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

uint16_t calcCoderFreq(float freq) // from freq value to coder value
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

void voicesInit(Voice* voices,uint16_t coderF,uint8_t cga)
{
    for(uint8_t v=0;v<VOICES_NB;v++){
        voices[v].maxCoderFreq=10000;
        voices[v].genAmpl=0x7fff;

        voices[v].genAmpl=amplLevel[cga];
        voices[v].coderGenAmpl=cga;
        voices[v].maxCoderGenAmpl=MAX_16B_LINEAR_VALUE;

        voices[v].coderFreq=coderF;
        float f=calcFreq(voices[v].coderFreq);          // 440Hz
        setVoiceFrequency(f,&voices[v]);    

        voices[v].sampleNbToFill=SAMPLE_BUFFER_SIZE;    
        voices[v].currentSample=0;
        voices[v].currEch=0;
        voices[v].currEchFra=0;

        voices[v].noisePhase = 0;           // Q16.16
        voices[v].noiseStep  = 60817408;    // Q16.16

        for(uint8_t i=0;i<W_NB;i++){
            voices[v].coderAmpl[i]=0;
            voices[v].maxCoderAmpl[i]=MAX_16B_LINEAR_VALUE-1;
            voices[v].basicWaveAmpl[i]=0;
            voices[v].coderSw[i]=0;
        }
    }
}

void voicesInit(Voice* voices,float freq,uint8_t cga){
   voicesInit(voices,calcCoderFreq(freq),cga);
} 

void dumpVoices(Voice* v)
{
  printf("   frequency(c/M/f)   sampleNb currSample stepInt stepFra currEch currEchFra noisePhase noiseStep                                    WaveAmpl(c-M-b)                                             genAmpl(c=M=g)     switchs     \n");
  for(uint8_t n=0;n<VOICES_NB;n++){  
    printf("%d %d-%d-%4.3f    %d       %d        %d      %d      %d       %d           %d        %d  ",n,v[n].coderFreq,v[n].maxCoderFreq,v[n].frequency,v[n].sampleNbToFill,v[n].currentSample,v[n].stepInt,v[n].stepFra,v[n].currEch,v[n].currEchFra,v[n].noisePhase,v[n].noiseStep);
    for(uint8_t wa=0;wa<BASIC_WAVES_NB;wa++){printf("%d-%d-%d ",v[n].coderAmpl[wa],v[n].maxCoderAmpl[wa],v[n].basicWaveAmpl[wa]);}
    printf("%d=%d=%d ",v[n].coderGenAmpl,v[n].maxCoderGenAmpl,v[n].genAmpl);
    for(uint8_t sw=0;sw<CODER_NB;sw++){printf("%d ",v[n].coderSw[sw]);}
    printf("\n");
  }
}

// update voice[].frequency - compute steps
void setVoiceFrequency(float freq,Voice* v){
    
    v->frequency=freq;

    float k=(uint32_t)BASIC_WAVE_TABLE_LEN*v->frequency/SAMPLE_RATE;
    v->stepInt=(uint32_t)k;
    v->stepFra=(uint32_t)((k-v->stepInt)*MAX_STEP_FRA);
}

// update voice[].lfosFrequency - compute steps
void setLfosFrequency(float freq,uint8_t l){
    
    lfosFrequency[l]=freq;

    float k=(uint32_t)BASIC_WAVE_TABLE_LEN*lfosFrequency[l]/LFOS_SAMPLE_RATE;
    lfosStepInt[l]=(uint32_t)k;
    lfosStepFra[l]=(uint32_t)((k-lfosStepInt[l])*MAX_STEP_FRA);
}

void lfosInit(){
    for(uint8_t l=0;l<LFOS_NB;l++){

        coderLfos[l]=16;
        lfosFrequency[l]=calcFreq(coderLfos[l]);
        lfosMaxCoderFreq[l]=10000;
        currLfoEch[l]=0;
        currLfoEchFra[l]=0;
        lfosStepInt[l]=0;
        lfosStepFra[l]=0;

        sineLfo[l]=0;
        squareLfo[l]=0;        
        triangleLfo[l]=0;
        sawtoothLfo[l]=0;
    }
    memset(lfoScopeBuffer,0x0000,LFOS_NB*LFOS_SCOPE_BUFFER_LEN);
}

int32_t* waveformTable[]={sineWaveform,squareWaveform,triangleWaveform,sawtoothWaveform};
void lfosHandler()
{
  if((millisCounter-lfoTime)>lfoTimingInterval){
    lfoTime=millisCounter;
    for(uint8_t l=0;l<LFOS_NB;l++){

        uint16_t ce=currLfoEch[l];
        uint32_t cf=currLfoEchFra[l];

        cf += lfosStepFra[l];
        uint32_t carry = (cf > MAX_STEP_FRA);
        cf -= carry * MAX_STEP_FRA;
        ce += lfosStepInt[l] + carry;
        ce -= (ce >= BASIC_WAVE_TABLE_LEN) * BASIC_WAVE_TABLE_LEN;

        sineLfo[l]=sineWaveform[ce];
        squareLfo[l]=squareWaveform[ce];
        triangleLfo[l]=triangleWaveform[ce];
        sawtoothLfo[l]=sawtoothWaveform[ce];

        currLfoEch[l]=ce;
        currLfoEchFra[l]=cf;

        lfoScopeBuffer[l*LFOS_SCOPE_BUFFER_LEN+lfoScopeBufPtr]=ce;

//printf("l:%d si:%d sf:%d ce:%d ",l,lfosStepInt[l],lfosStepFra[l],ce);        
    }
    lfoScopeBufPtr++;
    lfoScopeBufPtr&=LFOS_SCOPE_BUFFER_LEN-1;
    //printf("\n");
  }
}

uint16_t getAmpl(Voice* v,uint8_t wav){
  //printf("coderAmpl:%d wav:%d lev:%d :%d\n",v->coderAmpl[wav],wav,amplLevel[v->coderAmpl[wav]],amplLevel[31]);
  return amplLevel[v->coderAmpl[wav]];
}

/* // 251uS
void __not_in_flash_func(fillVoiceBuffer)(volatile int32_t* vBuffer,Voice* v,uint8_t what,uint8_t bufNum){   // 5.8mS pour les 6 sources @512 samples (23mS@44100Hz)
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
    int32_t  waveAmplPnk  = v->basicWaveAmpl[W_PINK_NOISE];
    volatile int32_t* voiceBuffer=&vBuffer[0];

    //printf("sin:%d\n",v->basicWaveAmpl[W_SINUS]);

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
          
        pre += (int16_t)pink_state * waveAmplPnk;

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

void fillVoices()
{
    gpio_put(TST_PIN,1);

    if(i2s_buf_free[0]){fillVoiceBuffer(i2s_buffer[0],&voices[0],0,0);i2s_buf=i2s_buffer[0];}
    if(i2s_buf_free[1]){fillVoiceBuffer(i2s_buffer[1],&voices[0],0,1);i2s_buf=i2s_buffer[1];}

    gpio_put(TST_PIN,0);
}*/

/*void __not_in_flash_func(fillVoiceBuffer)(volatile int32_t* vBuffer,Voice* v,uint8_t bufNum){   // 5.8mS pour les 6 sources @512 samples (23mS@44100Hz)
//gpio_put(TST_PIN,HIGH);

      int16_t tablech[SAMPLE_BUFFER_SIZE];
      memset((void*)vBuffer,0x00,SAMPLE_BUFFER_SIZE*4*2);

      i2s_buf_free[bufNum]=false;


      uint8_t n=0;
      uint32_t currEch      = v[n].currEch;
      uint32_t currEchFra   = v[n].currEchFra;
      uint32_t stepInt      = v[n].stepInt;
      uint32_t stepFra      = v[n].stepFra;
      nPhase       = v[n].noisePhase;
      nStep        = v[n].noiseStep;
      uint32_t limit = (uint32_t)N << 16;
      int32_t  genAmpl      = v[n].genAmpl;
      int32_t  waveAmplSin  = v[n].basicWaveAmpl[W_SINUS];
      int32_t  waveAmplTri  = v[n].basicWaveAmpl[W_TRIANGLE];
      int32_t  waveAmplSaw  = v[n].basicWaveAmpl[W_SAWTOOTH];        
      int32_t  waveAmplSqr  = v[n].basicWaveAmpl[W_SQUARE];    
      int32_t  waveAmplWhi  = v[n].basicWaveAmpl[W_WHITE_NOISE];
      int32_t  waveAmplPnk  = v[n].basicWaveAmpl[W_PINK_NOISE];

  for(uint8_t n=0;n<VOICES_NB;n++){


      volatile int32_t* voiceBuffer=vBuffer;

      uint32_t s = SAMPLE_BUFFER_SIZE;

      do                                                          // compute & store samples nb in waves tables
      {
        s--;
        currEchFra += stepFra;
        uint32_t carry = (currEchFra > MAX_STEP_FRA);
        currEchFra -= carry * MAX_STEP_FRA;
        currEch += stepInt + carry;
        currEch &= BASIC_WAVE_TABLE_LEN-1;

        tablech[s]=currEch;
      }
      while (s!=0);

      for(uint32_t s = 0; s < SAMPLE_BUFFER_SIZE; s++)            // accumulate amplified samples + noises ... 1 voice
      {
        uint16_t e=tablech[s];
        int32_t pre = sineWaveform[e] * waveAmplSin;
        pre += triangleWaveform[e]  * waveAmplTri;
        pre += sawtoothWaveform[e]  * waveAmplSaw;
        pre += squareWaveform[e]    * waveAmplSqr;

        // noises
        nPhase += nStep;
        uint32_t tmp = nPhase - limit;
        nPhase = tmp + ((tmp >> 31) & limit);
        int32_t white = noise_table[nPhase>>16];                              // bruit blanc
        pink_state=(alpha * pink_state + (32768 - alpha) * (white)) >> 15;    // bruit rose

        pre += white * waveAmplWhi;        
        pre += (int16_t)pink_state * waveAmplPnk;

        *voiceBuffer+=pre;    // *voiceBuffer=pre;
        voiceBuffer++;
        *voiceBuffer+=pre;    // *voiceBuffer=pre;
        voiceBuffer++;
      }

      v[n].currEch    = currEch;
      v[n].currEchFra = currEchFra;
      v[n].noisePhase = nPhase; 
  }  

//gpio_put(TST_PIN,LOW);    
}*/

void __not_in_flash_func(fillVoiceBuffer)(volatile int32_t* vBuffer, Voice* v, uint8_t bufNum)
{
    // --- 1) Buffer local non-volatile pour accumulation ---
    static int32_t mixBuffer[SAMPLE_BUFFER_SIZE * 2];
    memset(mixBuffer, 0, SAMPLE_BUFFER_SIZE * 2 * sizeof(int32_t));

    // --- 2) Boucle des voix ---
    for (uint8_t n = 0; n < VOICES_NB; n++)
    {
        // Lecture des paramètres de la voix (optimisé)
        uint32_t currEch    = v[n].currEch;
        uint32_t currEchFra = v[n].currEchFra;
        uint32_t stepInt    = v[n].stepInt;
        uint32_t stepFra    = v[n].stepFra;

        uint32_t nPhase     = v[n].noisePhase;
        uint32_t nStep      = v[n].noiseStep;
        uint32_t limit      = (uint32_t)N << 16;

        int32_t waveAmplSin = v[n].basicWaveAmpl[W_SINUS];
        int32_t waveAmplTri = v[n].basicWaveAmpl[W_TRIANGLE];
        int32_t waveAmplSaw = v[n].basicWaveAmpl[W_SAWTOOTH];
        int32_t waveAmplSqr = v[n].basicWaveAmpl[W_SQUARE];
        int32_t waveAmplWhi = v[n].basicWaveAmpl[W_WHITE_NOISE];
        int32_t waveAmplPnk = v[n].basicWaveAmpl[W_PINK_NOISE];

        // --- 3) Boucle des samples ---
        for (uint32_t s = 0; s < SAMPLE_BUFFER_SIZE; s++)
        {
            // DDS
            currEchFra += stepFra;
            uint32_t carry = (currEchFra > MAX_STEP_FRA);
            currEchFra -= carry * MAX_STEP_FRA;
            currEch += stepInt + carry;
            currEch &= BASIC_WAVE_TABLE_LEN - 1;

            // Formes d’onde
            int32_t pre = 0;
            pre += sineWaveform[currEch]     * waveAmplSin;
            pre += triangleWaveform[currEch] * waveAmplTri;
            pre += sawtoothWaveform[currEch] * waveAmplSaw;
            pre += squareWaveform[currEch]   * waveAmplSqr;

            // Bruits
            nPhase += nStep;
            uint32_t tmp = nPhase - limit;
            nPhase = tmp + ((tmp >> 31) & limit);

            int32_t white = noise_table[nPhase >> 16];
            pink_state = (alpha * pink_state + (32768 - alpha) * white) >> 15;

            pre += white * waveAmplWhi;
            pre += (int16_t)pink_state * waveAmplPnk;

            // --- Accumulation SANS pénalité ---
            mixBuffer[s*2]     += pre;
            mixBuffer[s*2 + 1] += pre;
        }

        // Sauvegarde état voix
        v[n].currEch    = currEch;
        v[n].currEchFra = currEchFra;
        v[n].noisePhase = nPhase;
    }

    // --- 4) Copie finale vers buffer I2S (une seule fois) ---
    for (uint32_t i = 0; i < SAMPLE_BUFFER_SIZE * 2; i++)
        vBuffer[i] = mixBuffer[i];
}


void fillVoices()
{
    gpio_put(TST_PIN,1);

    if(i2s_buf_free[0]){fillVoiceBuffer(i2s_buffer[0],voices,0);i2s_buf_scope=i2s_buffer[0];}
    if(i2s_buf_free[1]){fillVoiceBuffer(i2s_buffer[1],voices,1);i2s_buf_scope=i2s_buffer[1];}

    gpio_put(TST_PIN,0);
}

