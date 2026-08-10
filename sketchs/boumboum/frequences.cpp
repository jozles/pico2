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
#include "rc_33tables.h"
#include "input_tables_management.h"
#include "sound_level_management.h"
#include "hardware/sync.h"



const uint8_t octNb = OCTNB;
float baseFreq = FREQ0;
float octFreq[octNb+1];

const uint16_t octIncrNb = 409;
float octIncr[octIncrNb];

extern uint32_t millisCounter;

extern uint16_t amplLevel[];

// current lfo values (lfoHandler triger'd by pwmIrqHandler)
float       lfosFrequency[MAX_LFO];                   // frequency
uint16_t    lfosCodersFreq[MAX_LFO];                  // frequency coder value
uint16_t    lfosMaxCoderFreq[MAX_LFO];                // frequency coder max value
uint16_t    lfosCodersFreqAtt[MAX_LFO];               // frequency input attenuator value

uint16_t    lfosStepInt[MAX_LFO];                     // partie entière du step lfo
uint32_t    lfosStepFra[MAX_LFO];                     // partie fractionnaire du step lfo
uint16_t    lfosStepIntD[MAX_LFO];                    // partie entière du step descendant
uint32_t    lfosStepFraD[MAX_LFO];                    // partie fractionnaire du step descendant  
uint16_t    currLfoEch[MAX_LFO];
uint32_t    currLfoEchFra[MAX_LFO];
int16_t     lfosOutputsValues[MAX_LFO][MAX_OUTPUTS_PER_OBJ];  // inutilisé ??
uint32_t    lfoTime=0;
uint32_t    lfoTimingInterval=1000/LFOS_SAMPLE_RATE;
int32_t     lfoScopeBuffer[MAX_LFO*OSC_SCOPE_BUFFER_LEN];   // n° echantillons+rc_table 
int32_t     lfoScopeBufReal[MAX_LFO*OSC_SCOPE_BUFFER_LEN*BASIC_WAVES_NB];  // real values
uint16_t    lfoScopeBufPtr=0;

uint16_t    lfosCycleR[MAX_LFO];                      // cycle ratio 
uint16_t    lfosCoderCycleR[MAX_LFO];                 // cycle ratio coder value -64/+64

uint16_t    lfosCoderCycleRAtt[MAX_LFO];              // cycle ratio input attenuator value

int16_t     lfo_ctl_input_id[MAX_LFO][MAX_INPUTS_PER_OBJ];    // id des inputs du lfo dans ctl_input_xxx[]
int16_t     lfo_ctl_output_id[MAX_LFO][MAX_OUTPUTS_PER_OBJ];  // id des outputs du lfo dans ctl_output_xxx[]
int16_t     lfo_first_output_id;
int16_t     lfo_first_input_id;

extern int16_t ctl_input_val[MAX_INPUTS];
extern int16_t ctl_output_id_chain[MAX_OUTPUTS];

int32_t     voicesScopeDataBuffer[MAX_VOICES*SAMPLES_PER_BUFFER];  // all voices data buffer : 16bits low currech nb, 16 bits high rc table nb 

// i2s

Voice voices[MAX_VOICES];
int32_t w0[MAX_VOICES][BASIC_WAVES_NB];

extern int i2s_dma_chan0;
extern int i2s_dma_chan1;

extern volatile bool i2s_buf_free[];
extern int32_t* i2s_buffer[];
volatile int32_t* i2s_buf_scope;       // last loaded buffer for scope

// ********************** sine_table ******************************

int16_t basic_sine_table[BASIC_WAVE_TABLE_LEN];

void init_basic_sine_table(void)
{
    for (uint32_t i = 0; i < BASIC_WAVE_TABLE_LEN; i++)
    {
        // phase 0..2π
        float phase = (float)i * (2.0f * M_PI / BASIC_WAVE_TABLE_LEN);

        // sinus Q15
        float s = sinf(phase);
        basic_sine_table[i] = (int16_t)(s * 32767.0f);
    }
}    

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

// tableau des fréquences d'octaves
void showOctFreq() 
{ 
  printf("  fréquences des octaves\n");
  for (uint16_t i = 0; i < octNb; i++ ){
    printf("%d: %5.3f - ",i,octFreq[i]);
  }
  printf("\n");
}

void fillOctFreq() { 
  for (uint8_t i = 0; i <= octNb; i++) {
    octFreq[i] = baseFreq * (1<<i); 
  }
  //showOctFreq();  
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
  printf("sound_tables_init\n");
  
init_basic_sine_table();

  fillOctFreq();
  fillOctIncr();
  init_noise();
  fillAmplIncr();
}

// **********************  voices ************************

void voicesInit(Voice* voices,uint16_t coderF,uint8_t cga)    // cga = genAmpl level
{
    printf("%u voices init\n",MAX_VOICES);
    for(uint8_t v=0;v<MAX_VOICES;v++){
        voices[v].coderCycleR=MAXCODER_RC/2;
        voices[v].cycleR=voices[v].coderCycleR;
        voices[v].coderCycleRAtt=FULL_ATTENUATION_VALUE;

        voices[v].genAmpl=amplLevel[cga];
        voices[v].coderGenAmpl=cga;
        //voices[v].maxCoderGenAmpl=MAX_16B_LINEAR_VALUE;

        voices[v].coderFreq=coderF;
        float f=calcFreq(voices[v].coderFreq);          // 440Hz
        voices[v].basicFrequency=f;
        setVoiceFrequency(f,&voices[v],voices[v].coderCycleR);
        voices[v].coderFreqAtt=FULL_ATTENUATION_VALUE;    

        voices[v].sampleNbToFill=SAMPLES_PER_BUFFER;    
        voices[v].currentSample=0;
        voices[v].currEch=0;
        voices[v].currEchFra=0;

        voices[v].noisePhase = 0;           // Q16.16
        voices[v].noiseStep  = 60817408;    // Q16.16

        for(uint8_t i=0;i<VCES_OUTPUTS_NB;i++){
            voices[v].coderWaveAmpl[i]=0;
            voices[v].basicWaveAmpl[i]=0;
            voices[v].waveAmplChge[i]=false;
            voices[v].coderSw[i]=0;
        }

        for(uint8_t w=0;w<BASIC_WAVES_NB;w++){  // init previous values for every waves
          w0[v][w]=-1;
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
    printf("%d %d-%d-%4.3f    %d       %d        %d      %d      %d       %d           %d        %d  ",n,v[n].coderFreq,MAXCODER_RC,v[n].frequency,v[n].sampleNbToFill,v[n].currentSample,v[n].stepInt,v[n].stepFra,v[n].currEch,v[n].currEchFra,v[n].noisePhase,v[n].noiseStep);
    for(uint8_t wa=0;wa<BASIC_WAVES_NB;wa++){printf("%d-%d-%d ",v[n].coderWaveAmpl[wa],VCES_MAX_FREQ_CODERS,v[n].basicWaveAmpl[wa]);}
    printf("%d=%d=%d ",v[n].coderGenAmpl,VCES_MAX_AMPL_CODERS,v[n].genAmpl);
    for(uint8_t sw=0;sw<CODER_NB;sw++){printf("%d ",v[n].coderSw[sw]);}
    printf("\n");
  }
}

uint16_t __not_in_flash_func(calcCoderFreq)(float freq) // from freq value to coder value
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
float __not_in_flash_func(calcFreq)(uint16_t val) // from lin value (0-octIncrNb*OCTNB) to snd value (baseF à baseF*2^OCTNB)
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
    printf("%u lfos init\n",MAX_LFO);
    for(uint8_t l=0;l<MAX_LFO;l++){

        lfosCodersFreq[l]=1768;    // 1.5s
        lfosFrequency[l]=calcFreq(lfosCodersFreq[l])/VOICE_FREQ_DIVIDER;
        lfosMaxCoderFreq[l]=LFOS_MAX_FREQ_CODERS;
        lfosCodersFreqAtt[l]=FULL_ATTENUATION_VALUE;
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
  if((millisCounter-lfoTime)>lfoTimingInterval){
    lfoTime=millisCounter;

    const int16_t *base32 = &rc_tables[32][0][0];                           // base table 32 pour saw
    //uint8_t src=LFOS_____;
    //src=src*MAX_OBJECTS;
    
    for(uint8_t l=0;l<MAX_LFO;l++){

        //src+=l;

        uint32_t ce=currLfoEch[l];                      
        uint32_t cf=currLfoEchFra[l];

        uint32_t rc = lfosCycleR[l]&63;                 // rc=0-63
        uint32_t rcTableNb = (rc <= 32) ? rc : (64 - rc);

        cf += lfosStepFra[l];
        uint32_t carry = (cf >= MAX_STEP_FRA);
        cf -= carry * MAX_STEP_FRA;
        ce += lfosStepInt[l] + carry;
        ce &= BASIC_WAVE_TABLE_LEN-1;

        currLfoEch[l]=ce;                               // long term value
        //lfoScopeBuffer[l*OSC_SCOPE_BUFFER_LEN+lfoScopeBufPtr]=ce+rc<<16;

        bool vv=(ce<RC_TABLES_LEN);
        int sign=(vv*2-1);                              // invert 180-360°

        ce ^= (!vv) * (BASIC_WAVE_TABLE_LEN - 1);       // ce = vv*ce+!vv*((BASIC_WAVE_TABLE_LEN-1) - currEch);  // invert 180-360°       
        ce &= RC_TABLES_LEN-1;

        uint32_t ce_idx = ce;
        if (rc > 32){ce_idx = (RC_TABLES_LEN - 1) - ce_idx;}
        
        const int16_t *w = &rc_tables[rcTableNb][0][0]+RC_N_WAVES*ce_idx;    // rc_table values ptr 
        
        int16_t c0=sign*w[LSIN];
        uint16_t out_id;
        out_id=ctl_output_id_chain[lfo_ctl_output_id[l][LSIN]];
        lfosOutputsValues[l][LSIN]=c0;
        if (__builtin_expect(out_id != NO_LINK, 0)){update_inputs(out_id,c0);}  //update_inputs(src,out_id,c0);}

        int16_t c1=sign*w[LTRI];
        out_id=ctl_output_id_chain[lfo_ctl_output_id[l][LTRI]];
        lfosOutputsValues[l][LTRI]=c1;
        if (__builtin_expect(out_id != NO_LINK, 0)){update_inputs(out_id,c1);}  //update_inputs(src,out_id,c1);}

        int16_t tri=base32[RC_N_WAVES*ce+LTRI];
        int32_t c2;
        if(ce<(RC_N_SAMPLES >> 1)){c2=(65536-tri)>>1;}
        else c2=tri>>1;
        c2=sign*c2;
        if(rc>=32){c2=-c2;}
        out_id=ctl_output_id_chain[lfo_ctl_output_id[l][LSAW]];
        lfosOutputsValues[l][LSAW]=c2;
        if (__builtin_expect(out_id != NO_LINK, 0)){update_inputs(out_id,c2);}  //update_inputs(src,out_id,c2);}

        int16_t c3=(currLfoEch[l] & (BASIC_WAVE_TABLE_LEN>>1)) ? -0x7fff : 0x7fff;
        out_id=ctl_output_id_chain[lfo_ctl_output_id[l][LSQR]];
        lfosOutputsValues[l][LSQR]=c3;
        if (__builtin_expect(out_id != NO_LINK, 0)){update_inputs(out_id,c3);}  //update_inputs(src,out_id,c3);}
        
        uint32_t bufScopeOffset=l*OSC_SCOPE_BUFFER_LEN*BASIC_WAVES_NB + lfoScopeBufPtr*BASIC_WAVES_NB;
        lfoScopeBufReal[bufScopeOffset+LSIN]=c0;
        lfoScopeBufReal[bufScopeOffset+LTRI]=c1;
        lfoScopeBufReal[bufScopeOffset+LSAW]=c2;
        lfoScopeBufReal[bufScopeOffset+LSQR]=c3;
        //if(l==0){printf("ptr:%d obj:%d c0:%i c1:%i c2:%i c3:%i\n",lfoScopeBufPtr,l,c0,c1,c2,c3);}
      }
    lfoScopeBufPtr++;
    lfoScopeBufPtr&=OSC_SCOPE_BUFFER_LEN-1;
    //printf("ptr:%d c0:%i c1:%i c2:%i c3:%i\n",lfoScopeBufPtr,lfoScopeBufReal[0*OSC_SCOPE_BUFFER_LEN*BASIC_WAVES_NB + lfoScopeBufPtr*BASIC_WAVES_NB+LSIN]);
  }
}

// ***************************  voices producer  ******************************

// 
void __not_in_flash_func(fillVoiceBuffer_mono)(volatile int32_t* vBuffer,Voice* v,uint8_t voiceNum){   // 360uS ; >900uS avec rc_tables en flash pour les 6 sources @512 samples (23mS@44100Hz)
  
      i2s_buf_scope=vBuffer;

// init noise
      nPhase       = v->noisePhase;
      nStep        = v->noiseStep;
      uint32_t limit = (uint32_t)NOISE_TABLE_SIZE << 16;

// init waves      
      uint32_t currEch      = v->currEch;
      uint32_t currEchFra   = v->currEchFra;
      uint32_t stepInt      = v->stepInt;
      uint32_t stepFra      = v->stepFra;

      int32_t* w0Sin = &w0[voiceNum][WSIN];

// init rc
      uint32_t rc = v->cycleR&63;                   // rc=0-63
      uint32_t rcTableNb = (rc <= 32) ? rc : (64 - rc);
      uint32_t rcnb16=rc<<16;

      const int16_t* rcTableCurr = &rc_tables[rcTableNb][0][0];
      const int16_t* rcTable32 = &rc_tables[32][0][0];                           // base table 32 pour saw      

// init waves ampl (pointers necessary for real time change)     
      volatile uint32_t* waveAmplSin    = &v->basicWaveAmpl[WSIN];
      volatile uint32_t* newWaveAmplSin = &v->newBasicWaveAmpl[WSIN];
      volatile bool* sinWaveAmplChge    = &v->waveAmplChge[WSIN];
      volatile int32_t*  diffSin        = &v->diffWaveAmpl[WSIN];
      volatile uint32_t* waveAmplTri    = &v->basicWaveAmpl[WTRI];
      volatile uint32_t* newWaveAmplTri = &v->newBasicWaveAmpl[WTRI];
      volatile bool* triWaveAmplChge    = &v->waveAmplChge[WTRI];      
      volatile uint32_t* waveAmplSaw    = &v->basicWaveAmpl[WSAW];
      volatile uint32_t* newWaveAmplSaw = &v->newBasicWaveAmpl[WSAW];
      volatile bool* sawWaveAmplChge    = &v->waveAmplChge[WSAW];              
      volatile uint32_t* waveAmplSqr    = &v->basicWaveAmpl[WSQR]; 
      volatile uint32_t* newWaveAmplSqr = &v->newBasicWaveAmpl[WSQR];
      volatile bool* sqrWaveAmplChge    = &v->waveAmplChge[WSQR];      
      volatile uint32_t* waveAmplWhi    = &v->basicWaveAmpl[WHIT];
      volatile uint32_t* waveAmplPnk    = &v->basicWaveAmpl[PONK];

      volatile uint16_t* waveAmplGen = &v->genAmpl;

      //if(voiceNum==0){printf("v:%u f:%f %i %i %i %i %i\n",voiceNum,v->frequency,waveAmplSin,waveAmplTri,waveAmplSaw,waveAmplSqr,waveAmplGen);}

// fast loop computing samples index
      uint32_t s = SAMPLES_PER_BUFFER;
      int32_t* vsBuffer=voicesScopeDataBuffer+voiceNum*SAMPLES_PER_BUFFER; // temporary buffer for fast currech computing       
      do {
        

        currEchFra += stepFra;
        uint32_t carry = (currEchFra >= MAX_STEP_FRA);
        currEchFra -= carry * MAX_STEP_FRA;
        currEch += stepInt + carry;
        currEch &= BASIC_WAVE_TABLE_LEN-1;      // currEch 0-2047

        vsBuffer[SAMPLES_PER_BUFFER-s]=currEch+rcnb16;             // currEch + rc table nb
        s--;
      }
      while (s!=0);

// waves gen + noises (filling i2s data)
      for(uint32_t s = 0; s < SAMPLES_PER_BUFFER; s++)
      {

        // !!!!! pour le scope un buffer séparé serait utile : !!!!! 
        // le scope affiche lentement et i2sbuf est modifié rapidement 

        // waves

        uint32_t ce=vsBuffer[s];          // ce : 16 bits gauche = rc, 16 bits droite num ech
        uint32_t rc=ce>>16; 
        
        ce &= (BASIC_WAVE_TABLE_LEN-1);                   // local currEch (cyclic ratio managment)

        bool vv=(ce<RC_TABLES_LEN);
        int sign=(vv*2-1);                                // invert 180-360°

        // if ce<RC_TABLES_LEN ce=ce else ce=(RC_TABLES_LEN - 1) - (ce - RC_TABLE_LEN) ... 2*RC_TABLE_LEN - ce - 1
        ce ^= (!vv) * (BASIC_WAVE_TABLE_LEN - 1);         // ce = vv*ce+!vv*((BASIC_WAVE_TABLE_LEN-1) - ce);  // invert 180-360°           
        ce &= RC_TABLES_LEN-1;

        uint32_t ce_idx = ce;
        if (rc > 32) ce_idx = (RC_TABLES_LEN - 1) - ce_idx;
        const int16_t* w=rcTableCurr+RC_N_WAVES*ce_idx;   // rc_table values ptr

        int32_t pre=0;

        int16_t wwave=w[WSIN];
        /*
        if (__builtin_expect(*sinWaveAmplChge ,false)){
            *sinWaveAmplChge=false;
            *w0Sin=1;
            *diffSin=(*newWaveAmplSin-*waveAmplSin)/ *w0Sin;
        }

        if(__builtin_expect(*w0Sin>0,false)) {
            *waveAmplSin += *diffSin;
            *w0Sin--;
        } */           
        
        if (__builtin_expect(*sinWaveAmplChge && wwave<255,false)){  
            *waveAmplSin = *newWaveAmplSin;
        }          
        pre=(wwave * *waveAmplSin);     //>>GAIN_REDUC;

        if (__builtin_expect((*waveAmplTri + *newWaveAmplTri),false)!=0){
          wwave=w[WTRI];
          if (__builtin_expect(*triWaveAmplChge && wwave<255,false)){ 
              *waveAmplTri = *newWaveAmplTri;
          }
          pre += (wwave * *waveAmplTri);  //>>GAIN_REDUC;
        }

// saw et sqr semblent faire une fréquence double        

        if (__builtin_expect((*waveAmplSaw + *newWaveAmplSaw),false)!=0){
          int16_t tri=rcTable32[RC_N_WAVES*ce+WTRI];  // saw utilise la table 32 du triangle
          int32_t saw;
          if(ce<(RC_N_SAMPLES >> 1)){saw=(65536-tri)>>1;}
          else saw=tri>>1;
          if(rc>=32){saw=-saw;}                       // saw n'a pas de réglace de rc, juste une inversion de phase (montée ou descente verticale)
          if (__builtin_expect(*sawWaveAmplChge && saw<255,false)){  
              *waveAmplSaw = *newWaveAmplSaw;
          }        
          pre += (saw * *waveAmplSaw);     //>>GAIN_REDUC;
        }          

        if (__builtin_expect(*newWaveAmplSqr!=0,false)){
          int16_t sqr=(ce & (BASIC_WAVE_TABLE_LEN>>1)) ? -0x7fff : 0x7fff;    // sqr cr not implemented
          pre += (sqr * *newWaveAmplSqr);     //>>GAIN_REDUC;
        }

        //pre = pre>>8;
        pre *= sign;

        // noises

        nPhase += nStep;
        uint32_t tmp = nPhase - limit;
        nPhase = tmp + ((tmp >> 31) & limit);
        int32_t white = noise_table[nPhase>>16];
        pre += (white * *waveAmplWhi);     //>>GAIN_REDUC;

        // bruit rose 1-pôle branchless
        pink_state=(alpha * pink_state + (32768 - alpha) * (white)) >> 15;    
        pre += (pink_state * *waveAmplPnk);  //>>GAIN_REDUC;

        pre *= *waveAmplGen;

        *vBuffer+=pre;
        vBuffer++;
        *vBuffer+=pre;
        vBuffer++;
      }

    v->currEch    = currEch;
    v->currEchFra = currEchFra;
    v->noisePhase = nPhase;
}

void __not_in_flash_func(fillVoiceBuffer)(int32_t* vBuffer, Voice* voices, uint8_t bufNum)
{   
    blank_(vBuffer,SAMPLES_PER_BUFFER*2*4,0x00);   // env 12uS

    for(uint8_t v=0;v<MAX_VOICES;v++){   //MAX_VOICES;v++){
//gpio_put(TST_PIN,1);      
      fillVoiceBuffer_mono(vBuffer, &voices[v],v);
      
      //printf("v:%u\n",v);for(uint16_t b=0;b<1024;b++){printf("%u %i %X ; ",b,vBuffer[b],(uint32_t)vBuffer[b]);}printf("\n");
//gpio_put(TST_PIN,0);      
    }
    i2s_buf_free[bufNum] = false;
}


void fillVoices()
{
    if(i2s_buf_free[0]){
//gpio_put(TST_PIN,1);      
      fillVoiceBuffer(i2s_buffer[0],voices,0);
//gpio_put(TST_PIN,0);     
    }
    if(i2s_buf_free[1]){      
      fillVoiceBuffer(i2s_buffer[1],voices,1);
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

}*/
