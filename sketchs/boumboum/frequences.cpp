#include <stdio.h>
#include "pico/stdlib.h"
#include "frequences.h"
#include "util.h"
#include "const.h"
#include <math.h>

const uint8_t octNb = OCTNB;
float baseFreq = FREQ0;
float octFreq[octNb+1];
int16_t sineWaveform[BASIC_WAVE_TABLE_LEN];
int16_t squareWaveform[BASIC_WAVE_TABLE_LEN];
int16_t triangleWaveform[BASIC_WAVE_TABLE_LEN];
int16_t sawtoothWaveform[BASIC_WAVE_TABLE_LEN];
int16_t pinkNoiseWaveform[BASIC_WAVE_TABLE_LEN];
int16_t whiteNoiseWaveform[BASIC_WAVE_TABLE_LEN];

const uint16_t octIncrNb = 409;
float octIncr[octIncrNb];

uint8_t stepAmpl=MAX_16B_LINEAR_VALUE/16;     // nbre d'intervalles / 3db
uint16_t amplLevel[MAX_16B_LINEAR_VALUE];

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

uint32_t phase = 0;           // Q16.16
uint32_t step  = 60817408;    // Q16.16

const int32_t alpha = 32113;  // 0.98 en Q15

int32_t pink_state = 0;       // Q15 interne

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
    phase += step;
    uint32_t limit = (uint32_t)N << 16;

    // branchless wrap using subtraction and conditional negation
    uint32_t tmp = phase - limit;
    phase = tmp + ((tmp >> 31) & limit);

    *white = noise_table[phase>>16];

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

void voiceInit(float freq,Voice* v)
{
    v->sampleNbToFill=SAMPLE_BUFFER_SIZE;    
    v->currentSample=0;
    v->currEch=0;
    v->currEchFra=0;
    setNewFrequency(freq,v);
    for(uint8_t i=0;i<BASIC_WAVES_NB;i++){      // all waves off
      v->coderAmpl[i]=0;
      v->coderAmpl[i]=0;
      v->coderAmpl[i]=0;
    }
    v->noisePhase = 0;           // Q16.16
    v->noiseStep  = 60817408;    // Q16.16
}

void setNewFrequency(float freq,Voice* v){
    v->newFrequency=freq;

    float k=(uint32_t)BASIC_WAVE_TABLE_LEN*v->newFrequency/SAMPLE_RATE;
    v->newStepInt=(uint32_t)k;
    v->newStepFra=(uint32_t)((k-v->newStepInt)*MAX_STEP_FRA);
}

uint16_t getAmpl(Voice* v,uint8_t wav){
  return amplLevel[v->coderAmpl[wav]];
}

void fillVoiceBuffer(int32_t* vBuffer,Voice* v,uint8_t what){   // 3.7mS pour sinus ; 3.2mS pour 2 noises  ; <8mS pour les 6 ; @1024 samples (23mS@44100Hz)
gpio_put(TST_PIN,HIGH);

    uint32_t currEch      = v->currEch;
    uint32_t currEchFra   = v->currEchFra;
    uint32_t stepInt      = v->stepInt;
    uint32_t stepFra      = v->stepFra;
    uint32_t nPhase       = v->noisePhase;
    uint32_t nStep        = v->noiseStep;
    int32_t  genAmpl      = v->genAmpl;
    int32_t  waveAmplSin  = v->basicWaveAmpl[W_SINUS];
    int32_t  waveAmplTri  = v->basicWaveAmpl[W_TRIANGLE];
    int32_t  waveAmplSaw  = v->basicWaveAmpl[W_SAWTOOTH];        
    int32_t  waveAmplSqr  = v->basicWaveAmpl[W_SQUARE];    
    int32_t  waveAmplWhi  = v->basicWaveAmpl[W_WHITE_NOISE];
    int32_t  waveAmplPin  = v->basicWaveAmpl[W_PINK_NOISE];    

    for(uint32_t s = 0; s < v->sampleNbToFill; s++)
    {
        // avance DDS (branchless)
        currEchFra += stepFra;
        uint32_t carry = (currEchFra > MAX_STEP_FRA);
        currEchFra -= carry * MAX_STEP_FRA;
        currEch += stepInt + carry;

        currEch -= (currEch >= BASIC_WAVE_TABLE_LEN) * BASIC_WAVE_TABLE_LEN;

        vBuffer[s*2]   = sineWaveform[currEch] * waveAmplSin;
        vBuffer[s*2]  += triangleWaveform[currEch] * waveAmplTri;
        vBuffer[s*2]  += sawtoothWaveform[currEch] * waveAmplSaw;
        vBuffer[s*2]  += squareWaveform[currEch] * waveAmplSqr;

        //if(currEch>224 && currEch<298){printf("currech:%d sineWaveform[currEch]:%i waveAmplSin:%d vBuffer[s*2]:%i\n",currEch,sineWaveform[currEch],waveAmplSin,vBuffer[s*2]);}

        if(v->newFrequency!=0){
          stepInt=v->newStepInt;
          v->stepInt=stepInt;
          stepFra=v->newStepFra;
          v->stepFra=stepFra;
          v->frequency=v->newFrequency;
          v->newFrequency=0;
        }
    
        // noises

        nPhase += nStep;
        uint32_t limit = (uint32_t)N << 16;

        // branchless wrap using subtraction and conditional negation
        uint32_t tmp = nPhase - limit;
        nPhase = tmp + ((tmp >> 31) & limit);

        uint32_t white = noise_table[phase>>16];

        vBuffer[s*2] += white * waveAmplWhi;

        // bruit rose 1-pôle branchless
        pink_state=(alpha * pink_state + (32768 - alpha) * (white)) >> 15;
          
        vBuffer[s*2] += (int16_t)pink_state * waveAmplPin;
        vBuffer[s*2+1] = vBuffer[s*2];
    }

    v->currEch    = currEch;
    v->currEchFra = currEchFra;
    v->noisePhase = nPhase;

//dumpStr(vBuffer,256);
gpio_put(TST_PIN,LOW);    
}

  /*//
  int16_t lastEch=0;
  uint16_t s;

  for(s=0;s<v->sampleNbToFill;s++){
    v->currEch+=v->stepInt;
    v->currEchFra+=v->stepFra;
    if(v->currEchFra>MAX_STEP_FRA){v->currEchFra-=MAX_STEP_FRA;v->currEch++;}
    if(v->currEch>BASIC_WAVE_TABLE_LEN){v->currEch-=BASIC_WAVE_TABLE_LEN;}

    lastEch=sineWaveform[v->currEch];

    vBuffer[s*2]=lastEch*v->genAmpl;
    vBuffer[s*2+1]=vBuffer[s*2];

    if(v->newFrequency!=0){
        v->stepInt=v->newStepInt;
        v->stepFra=v->newStepFra;
        v->frequency=v->newFrequency;
        v->newFrequency=0;
    }
    v->currentSample=lastEch;
  }*/


/*void _fillVoiceBuffer(int32_t* sampleBuffer,Voice* v)
{
  //gpio_put(TEST_PIN,ON);

  uint32_t ech=0,prev_ech=0;  // ptr dans la table d'onde basique
  for(uint16_t i=0;i<v->sampleNbToFill;i++){
    float int_part;

    prev_ech=ech;
//gpio_put(TST_PIN,HIGH);
    // modff ~ 50% du temps de boucle avec une seule forme d'onde (5.6uS/10.5)  
    ech=(uint32_t)(modff(v->currentSample*v->freqRateRatio,&int_part)*BASIC_WAVE_TABLE_LEN); // ech nbr
    uint32_t ech1=v->currentSample*(uint32_t)(v->freqRateRatio)*BASIC_WAVE_TABLE_LEN; // ech nbr 
//gpio_put(TST_PIN,LOW);
//printf("ech:%d ech1:%d\n",ech,ech1);    
    // si changement de fréquence, synchro sur début table d'onde pour éviter les défauts de forme d'onde
    if((v->newFrequency!=0)&&(prev_ech>ech)){ 
      v->frequency=v->newFrequency;
      v->freqRateRatio=v->newFreqRateRatio;
      v->newFrequency=0;
      ech=0;
      v->currentSample=0;
    }

    sampleBuffer[i*2]=sineWaveform[ech]*v->genAmpl; // 11.6mS     
    ((sineWaveform[ech]*v->basicWaveAmpl[WAVE_SINUS]
      + squareWaveform[ech]*v->basicWaveAmpl[WAVE_SQUARE]
      + triangleWaveform[ech]*v->basicWaveAmpl[WAVE_TRIANGLE]
      + sawtoothWaveform[ech]*v->basicWaveAmpl[WAVE_SAWTOOTH]
      + whiteNoiseWaveform[ech]*v->basicWaveAmpl[WAVE_WHITENOISE]
      + pinkNoiseWaveform[ech]*v->basicWaveAmpl[WAVE_PINKNOISE]
      )
      /MAX_AMP_VAL
    )*v->genAmpl;

    sampleBuffer[i*2+1]=sampleBuffer[i*2]; // stereo

    v->currentSample++;
  }
  if(v->currentSample>=SAMPLE_RATE*10 && ech>=BASIC_WAVE_TABLE_LEN-4){v->currentSample=0;}   // re-init to avoid phase error

  //gpio_put(TEST_PIN,OFF);
  
}*/

