#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "frequences.h"
#include "util.h"
#include "bb_i2s.h"
#include "const.h"
#include "rc_tables.h"
#include "input_tables_management.h"

#ifdef __cplusplus
extern "C" {
#endif

void __not_in_flash_func(blank)(char *var, uint16_t len);    // 268uS pour 2048 bytes ; memcpy(var,0x00,len) 375uS ; dma env 60uS si nécessaire et le tft_frame_blank peut être utilisé

#ifdef __cplusplus
}
#endif

const uint8_t octNb = OCTNB;
float baseFreq = FREQ0;
float octFreq[octNb+1];
//int32_t sineWaveform[BASIC_WAVE_TABLE_LEN];
//int32_t squareWaveform[BASIC_WAVE_TABLE_LEN];
//int32_t triangleWaveform[BASIC_WAVE_TABLE_LEN];
//int32_t sawtoothWaveform[BASIC_WAVE_TABLE_LEN];
//int32_t pinkNoiseWaveform[BASIC_WAVE_TABLE_LEN];
//int32_t whiteNoiseWaveform[BASIC_WAVE_TABLE_LEN];

const uint16_t octIncrNb = 409;
float octIncr[octIncrNb];

uint8_t stepAmpl=MAX_16B_LINEAR_VALUE/16;     // nbre d'intervalles / 3db
uint16_t amplLevel[MAX_16B_LINEAR_VALUE];

extern uint32_t millisCounter;

// current lfo values (lfoHandler triger'd by pwmIrqHandler)
float       lfosFrequency[MAX_LFO];                   // current lfo freq
uint16_t    lfosCodersFreq[MAX_LFO];                      // last coder value for freq
uint16_t    lfosCodersAttFreq[MAX_LFO];               // coder pour atténuateur ctl_input_val (freq)
uint16_t    lfosMaxCoderFreq[MAX_LFO];                // pmax value for lfo coderFreq
uint16_t    lfosStepInt[MAX_LFO];                     // partie entière du step lfo
uint32_t    lfosStepFra[MAX_LFO];                     // partie fractionnaire du step lfo
uint16_t    lfosStepIntD[MAX_LFO];                    // partie entière du step descendant
uint32_t    lfosStepFraD[MAX_LFO];                    // partie fractionnaire du step descendant  
uint16_t    currLfoEch[MAX_LFO];
uint32_t    currLfoEchFra[MAX_LFO];
int16_t     lfosOutputsValues[MAX_LFO][MAX_OUTPUTS_PER_OBJ];
uint32_t    lfoTime=0;
uint32_t    lfoTimingInterval=1000/LFOS_SAMPLE_RATE;
int32_t     lfoScopeBuffer[MAX_LFO*OSC_SCOPE_BUFFER_LEN];   // n° echantillons+rc_table 
int32_t     lfoScopeBufReal[MAX_LFO*OSC_SCOPE_BUFFER_LEN*BASIC_WAVES_NB];  // real values
uint16_t    lfoScopeBufPtr=0;
uint16_t    lfosCoderCycleR[MAX_LFO];                 // rapport cyclique -64/+64 pour coder
uint16_t    lfosCycleR[MAX_LFO];                      // somme lfosCoderCycleR et ctl_input_val
uint16_t    lfosCoderCycleRAtt[MAX_LFO];              // coder pour atténuateur ctl_input_val  (cra)
int16_t     lfo_ctl_input_id[MAX_LFO][MAX_INPUTS_PER_OBJ];    // id des inputs du lfo dans ctl_input_xxx[]
int16_t     lfo_ctl_output_id[MAX_LFO][MAX_OUTPUTS_PER_OBJ];  // id des outputs du lfo dans ctl_output_xxx[]

extern int16_t ctl_input_val[MAX_INPUTS];

int32_t     voicesDataBuffer[MAX_VOICES*SAMPLE_BUFFER_SIZE];  // all voices data buffer : 16bits low currech nb, 16 bits high rc table nb 

// i2s

Voice voices[MAX_VOICES];

extern int i2s_dma_chan0;
extern int i2s_dma_chan1;

extern volatile bool i2s_buf_free[];
extern int32_t* i2s_buffer[];
volatile int32_t* i2s_buf_scope;       // last loaded buffer for scope



// **********************  noises  *********************************

#define NOISE_TABLE_SIZE 4093
int16_t noise_table[NOISE_TABLE_SIZE];

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
  for (int i = 0; i < NOISE_TABLE_SIZE; i++)
    noise_table[i] =  (int16_t)(xrnd() >> 16);
}

static inline void get_noise(int16_t *white, int16_t *pink)
{
    // --- Bruit blanc bande limitée ---
    nPhase += nStep;
    uint32_t limit = (uint32_t)NOISE_TABLE_SIZE << 16;

    // branchless wrap using subtraction and conditional negation
    uint32_t tmp = nPhase - limit;
    nPhase = tmp + ((tmp >> 31) & limit);

    *white = noise_table[nPhase>>16];

    // bruit rose 1-pôle branchless
    pink_state=(alpha * pink_state + (32768 - alpha) * (*white)) >> 15;
    *pink = (int16_t)pink_state;

}

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

// production des valeurs d'échantillon pour les différentes formes d'onde
/*void fillBasicWaveForms(){
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
}*/

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

// initialisation des tableaux pour permettre calcFreq()
void sound_tables_init()        
{  
  printf(" sound_tables_init\n");
  
  fillOctFreq();
  fillOctIncr();
  //fillBasicWaveForms();
  init_noise();
  fillAmplIncr();
}

// **********************  voices ************************

void voicesInit(Voice* voices,uint16_t coderF,uint8_t cga)
{
    for(uint8_t v=0;v<MAX_VOICES;v++){
        voices[v].maxCoderFreq=VCES_MAX_FREQ_CODERS;
        voices[v].genAmpl=0x7fff;
        voices[v].coderCycleR=MAXCODER_RC/2;
        voices[v].cycleR=voices[v].coderCycleR;
        voices[v].coderCycleRAtt=FULL_ATTENUATION_VALUE;

        voices[v].genAmpl=amplLevel[cga];
        voices[v].coderGenAmpl=cga;
        voices[v].maxCoderGenAmpl=MAX_16B_LINEAR_VALUE;

        voices[v].coderFreq=coderF;
        float f=calcFreq(voices[v].coderFreq);          // 440Hz
        setVoiceFrequency(f,&voices[v],voices[v].coderCycleR);
        voices[v].coderAttFreq=FULL_ATTENUATION_VALUE;    

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
  for(uint8_t n=0;n<MAX_VOICES;n++){  
    printf("%d %d-%d-%4.3f    %d       %d        %d      %d      %d       %d           %d        %d  ",n,v[n].coderFreq,v[n].maxCoderFreq,v[n].frequency,v[n].sampleNbToFill,v[n].currentSample,v[n].stepInt,v[n].stepFra,v[n].currEch,v[n].currEchFra,v[n].noisePhase,v[n].noiseStep);
    for(uint8_t wa=0;wa<BASIC_WAVES_NB;wa++){printf("%d-%d-%d ",v[n].coderAmpl[wa],v[n].maxCoderAmpl[wa],v[n].basicWaveAmpl[wa]);}
    printf("%d=%d=%d ",v[n].coderGenAmpl,v[n].maxCoderGenAmpl,v[n].genAmpl);
    for(uint8_t sw=0;sw<CODER_NB;sw++){printf("%d ",v[n].coderSw[sw]);}
    printf("\n");
  }
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

// calcul de la fréquence sonore à partir de la valeur linéaire
float calcFreq(uint16_t val) // from lin value (0-octIncrNb*OCTNB) to snd value (baseF à baseF*2^OCTNB)
{ 
  uint8_t oct = val/ octIncrNb;
  uint16_t incr = val % octIncrNb;
  float freq = octFreq[oct] +octIncr[incr]*(octFreq[oct+1]-octFreq[oct]);
  //printf("val:%d oct:%d incr:%d freq:%f\n",val,oct,incr,freq);
  return freq;
}

// update voice[].frequency - compute steps
void __not_in_flash_func(setVoiceFrequency)(float freq,Voice* v,int8_t rc){
    
    v->frequency=freq;
    v->cycleR=rc;

    float k=(float)BASIC_WAVE_TABLE_LEN*v->frequency/SAMPLE_RATE;

    v->stepInt = (uint32_t)k;
    v->stepFra = (uint32_t)((k - (float)v->stepInt) * (MAX_STEP_FRA));
}

// *************************** lfos ****************************

void __not_in_flash_func(setLfosFrequency)(float freq,uint8_t lfo,int8_t rc){ 
    
    lfosFrequency[lfo]=freq;
    lfosCycleR[lfo]=rc;

    float k = (float)BASIC_WAVE_TABLE_LEN * lfosFrequency[lfo] / LFOS_SAMPLE_RATE;

    lfosStepInt[lfo] = (uint32_t)k;
    lfosStepFra[lfo] = (uint32_t)((k - (float)lfosStepInt[lfo]) * (MAX_STEP_FRA));

}

void lfosInit(){
    for(uint8_t l=0;l<MAX_LFO;l++){

        lfosCodersFreq[l]=1768;    // 1.5s
        lfosFrequency[l]=calcFreq(lfosCodersFreq[l])/1000;
        lfosMaxCoderFreq[l]=LFOS_MAX_FREQ_CODERS;
        lfosCodersAttFreq[l]=FULL_ATTENUATION_VALUE;
        currLfoEch[l]=0;
        currLfoEchFra[l]=0;
        lfosStepInt[l]=0;
        lfosStepFra[l]=0;
        lfosStepIntD[l]=0;
        lfosStepFraD[l]=0;

        for(uint8_t v=0;v<MAX_OUTPUTS_PER_OBJ;v++){lfosOutputsValues[l][v]=0;}

        lfosCoderCycleR[l]=MAXCODER_RC/2;
        lfosCycleR[l]=lfosCoderCycleR[l];
        lfosCoderCycleRAtt[l]=FULL_ATTENUATION_VALUE;
        setLfosFrequency(lfosFrequency[l],l,lfosCoderCycleR[l]);        

        lfoTime=0;
    }
    memset(lfoScopeBuffer,0x0000,MAX_LFO*OSC_SCOPE_BUFFER_LEN);

}

void dumpLfos()
{
  for(uint8_t lfo=0;lfo<1;lfo++){
    printf("l:%d cf:%d fr:%f inf:%i\n",lfo,lfosCodersFreq[lfo],lfosFrequency[lfo],ctl_input_val[lfo_ctl_input_id[lfo][LTRI]]);
  }
}

void __not_in_flash_func(lfosHandler)()
{
  #define LIM90  BASIC_WAVE_TABLE_LEN/4
  #define LIM270 BASIC_WAVE_TABLE_LEN*3/4

  if((millisCounter-lfoTime)>lfoTimingInterval){
    lfoTime=millisCounter;
    
    for(uint8_t l=0;l<MAX_LFO;l++){

        uint32_t ce=currLfoEch[l];                      
        uint16_t cf=currLfoEchFra[l];

        uint32_t rcTableNb = lfosCycleR[l];        // rc==0-31-62
        uint32_t tscope=rcTableNb<<16;
        int8_t sign0 = (rcTableNb<=RC_TABLES_NB)*2-1;   // invert value if 32-62 table        
        if(rcTableNb>RC_TABLES_NB-1){rcTableNb=(RC_TABLES_NB-1)*2-rcTableNb;}

        cf += lfosStepFra[l];
        uint32_t carry = (cf >= MAX_STEP_FRA);
        cf -= carry * MAX_STEP_FRA;
        ce += lfosStepInt[l] + carry;
        ce &= BASIC_WAVE_TABLE_LEN-1;

        currLfoEch[l]=ce;                               // long term value
        lfoScopeBuffer[l*OSC_SCOPE_BUFFER_LEN+lfoScopeBufPtr]=ce+tscope;

        bool vv=(ce<RC_TABLES_LEN);
        int sign=sign0*(vv*2-1);                        // invert 180-360°

        ce ^= (!vv) * (BASIC_WAVE_TABLE_LEN - 1);       // ce = vv*ce+!vv*((BASIC_WAVE_TABLE_LEN-1) - currEch);  // invert 180-360°       
        ce &= RC_TABLES_LEN-1;
        
        const int16_t *w = &rc_tables[rcTableNb][0][0]+3*ce;    // rc_table values ptr

        int16_t c0=sign*w[LSIN];
        int16_t c1=sign*w[LTRI];
        int16_t c2=sign*w[LSAW];
        int16_t c3=(currLfoEch[l] & (BASIC_WAVE_TABLE_LEN>>1)) ? -0x7fff : 0x7fff;
        lfosOutputsValues[l][LSIN]=c0;update_inputs(lfo_ctl_output_id[l][LSIN],c0);
        lfosOutputsValues[l][LTRI]=c1;update_inputs(lfo_ctl_output_id[l][LTRI],c1); //if(l==1){printf("c:%i id:%i\n",c1,lfo_ctl_output_id[l][LTRI]);}
        lfosOutputsValues[l][LSAW]=c2;update_inputs(lfo_ctl_output_id[l][LSAW],c2);
        lfosOutputsValues[l][LSQR]=c3;update_inputs(lfo_ctl_output_id[l][LSQR],c3);
        lfoScopeBufReal[l*OSC_SCOPE_BUFFER_LEN*BASIC_WAVES_NB + lfoScopeBufPtr*BASIC_WAVES_NB+LSIN]=c0;
        lfoScopeBufReal[l*OSC_SCOPE_BUFFER_LEN*BASIC_WAVES_NB + lfoScopeBufPtr*BASIC_WAVES_NB+LTRI]=c1;
        lfoScopeBufReal[l*OSC_SCOPE_BUFFER_LEN*BASIC_WAVES_NB + lfoScopeBufPtr*BASIC_WAVES_NB+LSAW]=c2;
        lfoScopeBufReal[l*OSC_SCOPE_BUFFER_LEN*BASIC_WAVES_NB + lfoScopeBufPtr*BASIC_WAVES_NB+LSQR]=c3;
        //if(l==0){printf("ptr:%d obj:%d c0:%i c1:%i c2:%i c3:%i\n",lfoScopeBufPtr,l,c0,c1,c2,c3);}
      }
    lfoScopeBufPtr++;
    lfoScopeBufPtr&=OSC_SCOPE_BUFFER_LEN-1;
    //printf("ptr:%d c0:%i c1:%i c2:%i c3:%i\n",lfoScopeBufPtr,lfoScopeBufReal[0*OSC_SCOPE_BUFFER_LEN*BASIC_WAVES_NB + lfoScopeBufPtr*BASIC_WAVES_NB+LSIN]);
  }
}

uint16_t getAmpl(Voice* v,uint8_t wav){
  return amplLevel[v->coderAmpl[wav]];
}

// ***************************  voices producer  ******************************

// 
void __not_in_flash_func(fillVoiceBuffer_mono)(volatile int32_t* vBuffer,Voice* v,uint8_t voiceNum){   // 5.8mS pour les 6 sources @512 samples (23mS@44100Hz)

      #define LIM90  BASIC_WAVE_TABLE_LEN/4
      #define LIM270 BASIC_WAVE_TABLE_LEN*3/4

      uint32_t currEch      = v->currEch;
      uint32_t currEchFra   = v->currEchFra;
      uint32_t stepInt      = v->stepInt;
      uint32_t stepFra      = v->stepFra;
      nPhase       = v->noisePhase;
      nStep        = v->noiseStep;
      uint32_t limit = (uint32_t)NOISE_TABLE_SIZE << 16;
      int32_t  genAmpl      = v->genAmpl;

      int32_t* vsBuffer=voicesDataBuffer+voiceNum*SAMPLE_BUFFER_SIZE; // voicesDataBuffer stores 32 bits data for scope AND inside fillVoice 

      i2s_buf_scope=vBuffer;

      uint32_t rcTableNb = v->cycleR;
      int8_t sign0 = (rcTableNb<=RC_TABLES_NB)*2-1;                           // invert value if 32-62 table
      uint32_t tscope=rcTableNb<<16;                                          // t=0-30 31 32-62 ; 63 valeurs MAXCODER_RC=62
      if(rcTableNb>RC_TABLES_NB-1){rcTableNb=(RC_TABLES_NB-1)*2-rcTableNb;}   // 32->30 42->20 52->10 62->00
      const int16_t *p = &rc_tables[rcTableNb][0][0];  
      
      int32_t  waveAmplSin  = v->basicWaveAmpl[W_SINUS]*sign0;
      int32_t  waveAmplTri  = v->basicWaveAmpl[W_TRIANGLE]*sign0;
      int32_t  waveAmplSaw  = v->basicWaveAmpl[W_SAWTOOTH]*sign0;        
      int32_t  waveAmplSqr  = v->basicWaveAmpl[W_SQUARE];    
      int32_t  waveAmplWhi  = v->basicWaveAmpl[W_WHITE_NOISE];
      int32_t  waveAmplPnk  = v->basicWaveAmpl[W_PINK_NOISE];
      
      uint32_t s = SAMPLE_BUFFER_SIZE;

      /* ***** test triangle ***** */

      uint32_t tri_phase=0;
      uint32_t tri_step = (uint32_t)((v->frequency * 4294967296.0f) / 44100);

      // filling sample nbs
      do {
        s--;

        currEchFra += stepFra;
        uint32_t carry = (currEchFra >= MAX_STEP_FRA);
        currEchFra -= carry * MAX_STEP_FRA;
        currEch += stepInt + carry;
        currEch &= BASIC_WAVE_TABLE_LEN-1;      // currEch 0-2047

        vsBuffer[s]=currEch+tscope;             // currEch + rc table nb
      }
      while (s!=0);
      
      // filling i2s data

      for(uint32_t s = 0; s < SAMPLE_BUFFER_SIZE; s++)
      {

        uint16_t ce=vsBuffer[s];          // ce : 16 bits gauche = rc, 16 bits droite num ech
        uint32_t rc=ce>>16;
        uint32_t mask = (rc >= 32);       // pour saw        
        
        ce &= (BASIC_WAVE_TABLE_LEN-1);   // local currEch (cyclic ratio managment)

        bool vv=(ce<RC_TABLES_LEN);
        int sign=(vv*2-1);                                    // invert 180-360°
  
        ce ^= (!vv) * (BASIC_WAVE_TABLE_LEN - 1);             // ce = vv*ce+!vv*((BASIC_WAVE_TABLE_LEN-1) - ce);  // invert 180-360°           
        ce &= RC_TABLES_LEN-1;

        const int16_t* w=p+3*ce;                              // rc_table values ptr 

        int32_t pre=w[0]*waveAmplSin; 

        int32_t tri32 = w[1];
        pre += tri32*waveAmplTri;
        
        uint32_t tri_u = (uint32_t)(tri32 + 32767);   // 0..65534
        uint32_t saw = (tri32 ^ -mask) + mask;
        pre += saw*waveAmplSaw;

        int16_t sqr=(ce & (BASIC_WAVE_TABLE_LEN>>1)) ? -0x7fff : 0x7fff;    // sqr cr not implemented
        pre += sqr*waveAmplSqr;

        pre *= sign;

        // noises

        nPhase += nStep;
        uint32_t tmp = nPhase - limit;
        nPhase = tmp + ((tmp >> 31) & limit);
        int32_t white = noise_table[nPhase>>16];
        pre += white * waveAmplWhi;

        // bruit rose 1-pôle branchless
        pink_state=(alpha * pink_state + (32768 - alpha) * (white)) >> 15;    
        pre += (int16_t)pink_state * waveAmplPnk;

        *vBuffer+=pre;
        vBuffer++;
        *vBuffer+=pre;
        vBuffer++;
      }

    v->currEch    = currEch;
    v->currEchFra = currEchFra;
    v->noisePhase = nPhase;

}

void blank(char *var, uint16_t len);

/*void dma_clear(void* dst, uint32_t size_bytes) {
    static const uint32_t zero = 0;

    int chan = dma_claim_unused_channel(true);
    //printf("dma_clear_test chan = %d\n", chan);

    dma_channel_config c = dma_channel_get_default_config(chan);

    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, DREQ_FORCE);      // <<< au lieu de 0
    channel_config_set_irq_quiet(&c, true);

    dma_channel_configure(
        chan,
        &c,
        dst,
        &zero,
        size_bytes / 4,
        true
    );

    dma_channel_wait_for_finish_blocking(chan);
    dma_channel_unclaim(chan);
}*/

// <235uS/voix +265uS blank +35   (2 voix 771)
void __not_in_flash_func(fillVoiceBuffer)(int32_t* vBuffer, Voice* voices, uint8_t bufNum)
{
//gpio_put(TST_PIN,1);
    //dma_clear(vBuffer, SAMPLE_BUFFER_SIZE * 8); // ne fonctionne pas (690uS)
    blank((char*)vBuffer,SAMPLE_BUFFER_SIZE*8);
    //memset((char*)vBuffer,0x00,SAMPLE_BUFFER_SIZE*8);
//gpio_put(TST_PIN,0);
    for(uint8_t v=0;v<MAX_VOICES;v++){
//gpio_put(TST_PIN,1);      
      fillVoiceBuffer_mono(vBuffer, &voices[v],v); //                  dumpStr(vBuffer,256);
//gpio_put(TST_PIN,0);      
    }

    i2s_buf_free[bufNum] = false;
}

uint8_t se=0;
#define MAX_SE 4
void fillVoices()
{
    if(i2s_buf_free[0]){
gpio_put(TST_PIN,1);      
      fillVoiceBuffer(i2s_buffer[0],voices,0);
      i2s_buf_free[0] = false;
      if(i2s_buf_free[1]&&se>MAX_SE){system_error("fillVoices");}
      se=true;
gpio_put(TST_PIN,0);     
    }
    if(i2s_buf_free[1]){
gpio_put(TST_PIN,1);       
      fillVoiceBuffer(i2s_buffer[1],voices,1);
      i2s_buf_free[1] = false;
gpio_put(TST_PIN,0);       
    }
}

/*
void dmaDiags(uint8_t dma){

  printf("%u DMA0: busy=%d, trans=%u, read=0x%08x\n",
       dma,   
       dma_channel_is_busy(i2s_dma_chan0),
       dma_hw->ch[i2s_dma_chan0].transfer_count,
       dma_hw->ch[i2s_dma_chan0].read_addr);

  printf("%u DMA1: busy=%d, trans=%u, read=0x%08x\n",
       dma,
       dma_channel_is_busy(i2s_dma_chan1),
       dma_hw->ch[i2s_dma_chan1].transfer_count,
       dma_hw->ch[i2s_dma_chan1].read_addr);

}


uint8_t fillVoicesCnt0=0;
uint8_t fillVoicesCnt1=0;
void fillVoices()
{
    if(i2s_buf_free[0]){
gpio_put(TST_PIN,1); 
        fillVoicesCnt0++;       
        for (int i = 0; i < SAMPLE_BUFFER_SIZE*2; i++) {
            i2s_buffer[0][i] = 0x00FF00FF;   // n’importe quel pattern non nul
        }
        i2s_buf_free[0] = false;
        if(fillVoicesCnt0>20){dmaDiags(0);}
gpio_put(TST_PIN,0);          
    }
    if(i2s_buf_free[1]){
gpio_put(TST_PIN,1);
        fillVoicesCnt1++;        
        for (int i = 0; i < SAMPLE_BUFFER_SIZE*2; i++) {
            i2s_buffer[1][i] = 0x00FF00FF;
        }
        i2s_buf_free[1] = false;
        if(fillVoicesCnt1>20){dmaDiags(1);}
gpio_put(TST_PIN,0);          
    }
}*/
