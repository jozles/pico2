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

extern bool tb7;
//static bool filterTraceDone = false;

const uint8_t octNb = OCTNB;
float baseFreq = FREQ0;
float octFreq[octNb+1];

const uint16_t octIncrNb = 409;
float octIncr[octIncrNb];

#define FILTER_TANH_LEN  2049              // one entry per Q8 unit, covering -4..+4 directly, no interpolation needed
#define FILTER_TANH_HALF (FILTER_TANH_LEN / 2)

#define FILTER_STEPS_PER_OCT 32
#define FILTER_MAX_OCT       10
#define FILTER_TABLE_LEN     (FILTER_MAX_OCT*FILTER_STEPS_PER_OCT + 1)

#define FILTER_KNEE       65536.0f      // raw units where the soft saturation starts (tanh argument = 1)
#define FILTER_FRAC       8             // fractional bits kept inside the filter (Q8)
#define FILTER_PRE_SHIFT  15            // shift between the voice signal and the filter input
#define FILTER_BIAS (32768 + (FILTER_TANH_HALF << 16))   // rounding offset and table centre folded into one constant

#define FILTER_POLE_FACTOR 0.43498f      // sqrt(2^(1/4) - 1): -3 dB of 4 identical poles in cascade, in the tan domain (2 stages : 0,6436)

//extern const int32_t filterDTable[];

//__attribute__((section(".ram_d1")))
int32_t filterDTable[FILTER_TANH_LEN];
//__attribute__((section(".ram_d1")))
int32_t z0,z1,z2,z3;                    // filter state in registers (Q8)

float gTable[FILTER_TABLE_LEN];


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
uint32_t    lfoTime=1;                                // synchrone avec le handler adsr 
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
extern uint8_t ctl_input_update_type[MAX_INPUTS];

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

/*static inline void get_noise(int16_t *white, int16_t *pink)
{
    // --- Bruit blanc bande limitée ---
    nPhase += nStep;
    uint32_t limit = (uint32_t)NOISE_TABLE_SIZE << 16;

    if (nPhase >= limit) nPhase -= limit;      // wrap: nPhase + nStep is always below 2*limit

    *white = noise_table[nPhase>>16];

    // bruit rose 1-pôle branchless
    pink_state=(alpha * pink_state + (32768 - alpha) * (*white)) >> 15;
    *pink = (int16_t)pink_state;

}*/

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

void fillFilterTanH()
{
    for (int i = 0; i < FILTER_TANH_LEN; i++) {
        float q = (float)(i - FILTER_TANH_HALF) / 256.0f;       // tanh argument (u / knee)
        float a = fabsf(q);
        float d;                                                // q - tanh(q), in knee units
        if (a < 0.25f) {                                        // series: avoids the cancellation of q - tanh(q)
            float q2 = a * a;
            d = a * q2 * (1.0f/3.0f - q2 * (2.0f/15.0f - q2 * (17.0f/315.0f - q2 * (62.0f/2835.0f - q2 * (1382.0f/155925.0f)))));
        } else {
            d = a - tanhf(a);
        }
        float dq8 = d * (FILTER_KNEE * 256.0f * 2.0f);          // knee units -> raw Q8, doubled (see filterStage)
        filterDTable[i] = (int32_t)lroundf(q < 0 ? -dq8 : dq8);
    }
}//*/

void fillFilterGTable()
{
    for (int i = 0; i < FILTER_TABLE_LEN; i++) {
        float oct  = (float)i / FILTER_STEPS_PER_OCT;
        float freq = FREQ0 * powf(2.0f, oct);
        gTable[i]  = tanf(M_PI * freq / SAMPLE_RATE) / FILTER_POLE_FACTOR;
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

  fillFilterGTable();
  fillFilterTanH();
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
        //voices[v].basicFrequency=f;
        setVoiceFrequency(f,&voices[v],voices[v].coderCycleR);
        voices[v].coderFreqAtt=FULL_ATTENUATION_VALUE;    

        voices[v].sampleNbToFill=SAMPLES_PER_BUFFER;    
        voices[v].currentSample=0;
        voices[v].currEch=0;
        voices[v].currEchFra=0;

        voices[v].noisePhase = 0;           // Q16.16
        voices[v].noiseStep  = 60817408;    // Q16.16

        printf("voice %u: id=%d type=%d\n", v, voices[v].voice_ctl_input_id[VFIL], ctl_input_update_type[voices[v].voice_ctl_input_id[VFIL]]);
        filtersInit(v);

        for(uint8_t i=0;i<VCES_OUTPUTS_NB;i++){
            voices[v].coderWaveAmpl[i]=0;
            voices[v].coderWaveAmplAtt[i]=0;
            voices[v].basicWaveAmpl[i]=0;
            voices[v].waveAmplChge[i]=false;
            //voices[v].coderSw[i]=0;
        }

        for(uint8_t w=0;w<BASIC_WAVES_NB;w++){  // init previous values for every waves
          w0[v][w]=-1;
        }
    }
printf("amplLevel[0]=%u [1]=%u [31]=%u | v0: coderFilterFreq=%d newFilterG=%ld filterG=%ld\n",
    amplLevel[0], amplLevel[1], amplLevel[31],
    voices[0].coderFilterFreq, voices[0].newFilterG, voices[0].filterG);
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
    //for(uint8_t sw=0;sw<CODER_NB;sw++){printf("%d ",v[n].coderSw[sw]);}
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

// *************************** filters ****************************

const float FILTER_MAX_FREQ = FREQ0 * 1024.0f;    // FREQ0 * 2^FILTER_MAX_OCT, reste sous Nyquist
const float FILTER_MIN_FREQ = 20.0f;

void filtersInit(uint8_t v)
{
    setVoiceFilter(&voices[v], (int16_t)(FILTER_MAX_OCT * octIncrNb), 0, NO_ATTENUATION_VALUE);   // cutoff grand ouvert par défaut ; atten wide open
    voices[v].filterG      = voices[v].newFilterG;
    voices[v].filterStages = 4;
    memset(voices[v].filter.z, 0, sizeof(voices[v].filter.z));
}

float __not_in_flash_func(calcFilterFreq)(int32_t code)                         // cutoff in Hz for a filter code, same domain and limits as calcFilterG
{
    if (code < 0) code = 0;
    if (code > FILTER_MAX_OCT * octIncrNb) code = FILTER_MAX_OCT * octIncrNb;   // the filter stops at ~16.7 kHz
    return calcFreq((uint16_t)code);
}

void __not_in_flash_func(setFilterFrequency)(float freqHz, Voice* v)
{
    if (freqHz < FILTER_MIN_FREQ) freqHz = FILTER_MIN_FREQ;
    if (freqHz > FILTER_MAX_FREQ) freqHz = FILTER_MAX_FREQ;

    v->filterFrequency = freqHz;

    float g = tanf(M_PI * freqHz / SAMPLE_RATE) / FILTER_POLE_FACTOR;           // frequency warping, done once per call (not per sample)
    v->newFilterG = (int32_t)(g / (1.0f + g) * 32768.0f);                       // gg = g/(1+g), always < 1: the value filterProcess expects
}

float __not_in_flash_func(calcFilterG)(int32_t val)
{
    float octPos = (float)val / (float)octIncrNb;
    if (octPos < 0) octPos = 0;
    if (octPos > FILTER_MAX_OCT) octPos = FILTER_MAX_OCT;

    float idxF = octPos * FILTER_STEPS_PER_OCT;
    int   idx  = (int)idxF;
    if (idx >= FILTER_TABLE_LEN - 1) idx = FILTER_TABLE_LEN - 2;
    float frac = idxF - idx;

    return gTable[idx] + frac * (gTable[idx+1] - gTable[idx]);
}

void __not_in_flash_func(setVoiceFilter)(Voice* v, int16_t coderFilterF, int16_t coderFilterFAtt, int16_t coderFilterLevAtt)
{
    v->coderFilterFreq    = coderFilterF;
    v->coderFilterFreqAtt = coderFilterFAtt;
    v->coderFilterLevAtt  = coderFilterLevAtt;

    v->filterFrequency = calcFilterFreq(coderFilterF);     // display only: cutoff set by the coder (modulation not included)

    int16_t id = v->voice_ctl_input_id[VFIL];    
    update_inputs(id, ctl_input_val[id]);
}

/*static inline __attribute__((always_inline)) int32_t filterStage(int32_t& z, int32_t x, int32_t ggS)   // one pole; x, z and result in Q8; ggS = g/(1+g) in Q31
{
    int32_t u = x - z;
    if (u >  67108863) u =  67108863;                  // +-2^26 = +-4 knees: beyond that the output is fully saturated
    if (u < -67108864) u = -67108864;                  // (the compiler merges these two lines into one ssat instruction)
    int32_t t2 = 2 * u - filterDTable[(u + FILTER_BIAS) >> 16];   // 2 * (straight line minus the tanh deviation); the table is doubled
    int32_t v;
    asm("smmulr %0, %1, %2" : "=r"(v) : "r"(ggS), "r"(t2));      // v = gg * t in Q8: high word of the 64-bit product, rounded
    x  = z + v;                                        // stage output
    z  = x + v;                                        // new state
    return x;
}*/

static inline __attribute__((always_inline)) int32_t smmulr(int32_t a, int32_t b)   // high word of a*b, rounded: one instruction on the Cortex-M33
{
    int32_t r;
    asm("smmulr %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
    return r;
}

static inline __attribute__((always_inline)) int32_t filterStage(int32_t& z, int32_t x, int32_t ggS)   // saturating pole; x, z, result in Q8; ggS = g/(1+g) in Q31
{
    int32_t u = x - z;
    asm("ssat %0, #27, %0" : "+r"(u));                 // saturation to [-2^26, 2^26-1] in one instruction (the compiler does not merge two ifs into it)
    int32_t v = smmulr(ggS, 2 * u - filterDTable[(u + FILTER_BIAS) >> 16]);   // the table is doubled
    x = z + v;                                         // stage output
    z = x + v;                                         // new state
    return x;
}

static inline __attribute__((always_inline)) int32_t filterStageLin(int32_t& z, int32_t x, int32_t ggS)   // linear pole (no saturation); x, z, result in Q8
{
    int32_t v = smmulr(ggS, 2 * (x - z));
    x = z + v;
    z = x + v;
    return x;
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
        if (rc > (RC_N_TABLES-1)){ce_idx = (RC_TABLES_LEN - 1) - ce_idx;}
        
        const int16_t *w = &rc_tables[rcTableNb][0][0]+RC_N_WAVES*ce_idx;    // rc_table values ptr 
        
        int16_t c0=sign*w[LSIN];
        uint16_t out_id;
        out_id=ctl_output_id_chain[lfo_ctl_output_id[l][LSIN]];
        lfosOutputsValues[l][LSIN]=c0;
        if (__builtin_expect(out_id != NO_LINK, 0)){update_inputs(out_id,c0);}

        int16_t c1=sign*w[LTRI];
        out_id=ctl_output_id_chain[lfo_ctl_output_id[l][LTRI]];
        lfosOutputsValues[l][LTRI]=c1;
        if (__builtin_expect(out_id != NO_LINK, 0)){update_inputs(out_id,c1);}

        int16_t tri=base32[RC_N_WAVES*ce+LTRI];
        int32_t c2;
        if(ce<(RC_N_SAMPLES >> 1)){c2=(65536-tri)>>1;}
        else c2=tri>>1;
        c2=sign*c2;
        if(rc>=32){c2=-c2;}
        out_id=ctl_output_id_chain[lfo_ctl_output_id[l][LSAW]];
        lfosOutputsValues[l][LSAW]=c2;
        if (__builtin_expect(out_id != NO_LINK, 0)){update_inputs(out_id,c2);}

        int16_t c3=(currLfoEch[l] & (BASIC_WAVE_TABLE_LEN>>1)) ? -0x7fff : 0x7fff;
        out_id=ctl_output_id_chain[lfo_ctl_output_id[l][LSQR]];
        lfosOutputsValues[l][LSQR]=c3;
        if (__builtin_expect(out_id != NO_LINK, 0)){update_inputs(out_id,c3);}
        
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

uint32_t ints = save_and_disable_interrupts();
gpio_put(TST_PIN,1);
      
      i2s_buf_scope=vBuffer;

// init noise
      nPhase       = v->noisePhase;
      nStep        = v->noiseStep;
      int32_t pink_state_loc = pink_state;
      uint32_t limit = (uint32_t)NOISE_TABLE_SIZE << 16;

// init waves      
      uint32_t currEch      = v->currEch;
      uint32_t currEchFra   = v->currEchFra;
      uint32_t stepInt      = v->stepInt;
      uint32_t stepFra      = v->stepFra;

// init rc
      uint32_t rc = v->cycleR&63;                   // rc=0-63
      uint32_t rcTableNb = (rc <= 32) ? rc : (64 - rc);
      uint32_t rcnb16=rc<<16;
      bool     reverseCeIdx = (rc > 32);            // was tested every sample as "if (rc > 32)"
      int      sawRcSign    = (rc >= 32) ? -1 : 1;  // was tested every sample as "if(rc>=32){saw=-saw;}"

      int16_t* rcTableCurr = &rc_tables[rcTableNb][0][0];
      int16_t* rcTable32 = &rc_tables[32][0][0];                           // base table 32 pour saw      

// init waves ampl (pointers needed for real time changes)     
      volatile uint32_t* waveAmplSin    = &v->basicWaveAmpl[WSIN];
      volatile uint32_t* newWaveAmplSin = &v->newBasicWaveAmpl[WSIN];
      volatile int32_t*  diffSin        = &v->diffWaveAmpl[WSIN];
      volatile uint32_t* waveAmplTri    = &v->basicWaveAmpl[WTRI];
      volatile uint32_t* newWaveAmplTri = &v->newBasicWaveAmpl[WTRI];      
      volatile uint32_t* waveAmplSaw    = &v->basicWaveAmpl[WSAW];
      volatile uint32_t* newWaveAmplSaw = &v->newBasicWaveAmpl[WSAW];          
      volatile uint32_t* waveAmplSqr    = &v->basicWaveAmpl[WSQR]; 
      volatile uint32_t* newWaveAmplSqr = &v->newBasicWaveAmpl[WSQR];
      volatile uint32_t* waveAmplWhi    = &v->basicWaveAmpl[WHIT];
      volatile uint32_t* waveAmplPnk    = &v->basicWaveAmpl[PONK];

      volatile uint16_t* waveAmplGen    = &v->genAmpl;

      {
      volatile int32_t* vb=vBuffer;
  // fast loop computing samples index
        uint32_t s = SAMPLES_PER_BUFFER;
        int32_t* vsBuffer=voicesScopeDataBuffer+voiceNum*SAMPLES_PER_BUFFER; // temporary buffer for fast currech computation       
        do {
          
          currEchFra += stepFra;
          uint32_t carry = (currEchFra >= MAX_STEP_FRA);
          currEchFra -= carry * MAX_STEP_FRA;
          currEch += stepInt + carry;
          currEch &= BASIC_WAVE_TABLE_LEN-1;        // currEch 0-2047

          vsBuffer[SAMPLES_PER_BUFFER-s]=currEch; 
          s--;
        }
        while (s!=0);
  
  // filters
        int32_t filterG = v->filterG;
        z0 = v->filter.z[0];z1 = v->filter.z[1];z2 = v->filter.z[2];z3 = v->filter.z[3];
        int32_t attQ23 = (int32_t)v->coderFilterLevAtt << 23;      // input attenuator for the high-word multiply (255 << 23 < 2^31)

  // waves gen (filling i2s data)
        #define RAMP_SHIFT  7                                             // 32-sample sub-blocks = 0.73 ms at 44.1 kHz
        #define RAMP_LEN   (1u << RAMP_SHIFT)
        static_assert((SAMPLES_PER_BUFFER % RAMP_LEN) == 0, "SAMPLES_PER_BUFFER must be a multiple of RAMP_LEN");

        int32_t sinAmpl = *waveAmplSin;
        int32_t triAmpl = *waveAmplTri;
        int32_t sawAmpl = *waveAmplSaw;
      
        for (uint32_t sb = 0; sb < SAMPLES_PER_BUFFER; sb += RAMP_LEN)    // NEW: outer loop, one pass per sub-block
        {
            // filters
            int32_t       filterGTarget = v->newFilterG;
            int32_t       filterGInc    = filterGTarget - filterG;
            int32_t       filterGAcc    = filterG << RAMP_SHIFT;

            // anti-clic
            int32_t       sinTarget = *newWaveAmplSin;                    // NEW: read the target once per sub-block
            int32_t       sinInc    = sinTarget - sinAmpl;                // NEW: gap between target and current amplitude
            int32_t       sinAcc    = sinAmpl << RAMP_SHIFT;              // NEW: current amplitude times 32

            int32_t       triTarget = *newWaveAmplTri;                    // NEW: read the target once per sub-block
            int32_t       triInc    = triTarget - triAmpl;                // NEW: gap between target and current amplitude
            int32_t       triAcc    = triAmpl << RAMP_SHIFT;              // NEW: current amplitude times 32

            int32_t       sawTarget = *newWaveAmplSaw;                    // NEW: read the target once per sub-block
            int32_t       sawInc    = sawTarget - sawAmpl;                // NEW: gap between target and current amplitude
            int32_t       sawAcc    = sawAmpl << RAMP_SHIFT;              // NEW: current amplitude times 32   
            
            // muted sounds skip evaluated once per sub-block
            bool noiseOn = (*waveAmplWhi != 0) || (*waveAmplPnk != 0);
            
            bool wsinOn  = (*waveAmplSin != 0);            
            bool wtriOn  = (*waveAmplTri != 0);
            bool wsawOn  = (*waveAmplSaw != 0);                        
            bool wsqrOn  = (*waveAmplSqr != 0);             

            for(uint32_t s = sb; s < sb + RAMP_LEN; s++)
            {
                  // !!!!! pour le scope un buffer séparé serait utile : !!!!! 
                  // le scope affiche lentement et i2sbuf est modifié rapidement 

                  // waves

                  uint32_t ce=vsBuffer[s];          

                  bool vv=(ce<RC_TABLES_LEN);
                  int sign=(vv*2-1);                                // invert 180-360°

                  // if ce<RC_TABLES_LEN ce=ce else ce=(RC_TABLES_LEN - 1) - (ce - RC_TABLE_LEN) ... 2*RC_TABLE_LEN - ce - 1
                  ce ^= (!vv) * (BASIC_WAVE_TABLE_LEN - 1);         // ce = vv*ce+!vv*((BASIC_WAVE_TABLE_LEN-1) - ce);  // invert 180-360°           
                  ce &= RC_TABLES_LEN-1;

                  uint32_t ce_idx = ce;
                  if (reverseCeIdx) ce_idx = (RC_TABLES_LEN - 1) - ce_idx;
                  const int16_t* w=rcTableCurr+RC_N_WAVES*ce_idx;   // rc_table values ptr

                  int32_t pre=0;

                  int16_t wwave;
                  
                  //if (__builtin_expect((wsinOn),false)){                
                    wwave=w[WSIN];
                    sinAcc += sinInc;                           // per sample: add + shift
                    pre = wwave * (sinAcc >> RAMP_SHIFT);       // replaces pre=(wwave * *waveAmplSin);     
                  //}

                  if (__builtin_expect((wtriOn),false)){
                    wwave=w[WTRI];
                    triAcc += triInc;                           
                    pre += wwave * (triAcc >> RAMP_SHIFT);     
                  }

          // saw et sqr semblent faire une fréquence double à vérifier       

                  if (__builtin_expect((wsawOn),false)){
                    int16_t tri=rcTable32[RC_N_WAVES*ce+WTRI];  // saw utilise la table 32 du triangle
                    int32_t saw;
                    if(ce<(RC_N_SAMPLES >> 1)){saw=(65536-tri)>>1;}
                    else saw=tri>>1;
                    saw *= sawRcSign;                 // saw n'a pas de réglace de rc, juste une inversion de phase (montée ou descente verticale)
                    sawAcc += sawInc;                           
                    pre += saw * (sawAcc >> RAMP_SHIFT);        
                  }          

                  if (__builtin_expect((wsqrOn),false)){
                    int16_t sqr=(ce & (BASIC_WAVE_TABLE_LEN>>1)) ? -0x7fff : 0x7fff;    // sqr cr to be implemented
                    pre += (sqr * *newWaveAmplSqr); 
                  }

                  pre *= sign;
  
                  // noises

                  if (noiseOn) {

                    nPhase += nStep;
                    if (nPhase >= limit) nPhase -= limit;      // wrap: nPhase + nStep is always below 2*limit
                    int32_t white = noise_table[nPhase>>16];
                    pre += (white * *waveAmplWhi);

                    // bruit rose 1-pôle branchless
                    pink_state_loc=(alpha * pink_state_loc + (32768 - alpha) * (white)) >> 15;    
                    pre += (pink_state_loc * *waveAmplPnk);
                  }

///*
gpio_put(TST_PIN,0);
if(tb7){

  
    filterGAcc += filterGInc;
    int32_t ggS = (filterGAcc >> RAMP_SHIFT) << 16;                    // ramped gg in Q31
    int32_t xq;
    asm("smmulr %0, %1, %2" : "=r"(xq) : "r"(pre >> (FILTER_PRE_SHIFT - FILTER_FRAC)), "r"(attQ23));   // input attenuator
    xq *= 2;                                                           // xq = input * att / 256
    xq = filterStage(z0, xq, ggS);
    xq = filterStageLin(z1, xq, ggS);
    xq = filterStageLin(z2, xq, ggS);
    xq = filterStageLin(z3, xq, ggS);
    pre = xq * (1 << (FILTER_PRE_SHIFT - FILTER_FRAC));                // back to the voice scale
  //
  /*// 
        filterGAcc += filterGInc;
        int32_t ggS = (filterGAcc >> RAMP_SHIFT) << 16;

        int32_t xq;
        asm("smmulr %0, %1, %2"
            : "=r"(xq)
            : "r"(pre >> (FILTER_PRE_SHIFT - FILTER_FRAC)), "r"(attQ23));
        xq <<= 1;

        // ---- stage 0 ----
        {
            int32_t u = xq - z0;
            asm("ssat %0, #27, %0" : "+r"(u));   // saturation ±2^26
            int32_t t2 = (u << 1) - filterDTable[(u + FILTER_BIAS) >> 16];
            int32_t v;
            asm("smmulr %0, %1, %2" : "=r"(v) : "r"(ggS), "r"(t2));
            xq = z0 + v;
            z0 = xq + v;
        }

        // ---- stage 1 ----
        {
            int32_t u = xq - z1;
            asm("ssat %0, #27, %0" : "+r"(u));
            int32_t t2 = (u << 1) - filterDTable[(u + FILTER_BIAS) >> 16];
            int32_t v;
            asm("smmulr %0, %1, %2" : "=r"(v) : "r"(ggS), "r"(t2));
            xq = z1 + v;
            z1 = xq + v;
        }

        // ---- stage 2 ----
        {
            int32_t u = xq - z2;
            asm("ssat %0, #27, %0" : "+r"(u));
            int32_t t2 = (u << 1) - filterDTable[(u + FILTER_BIAS) >> 16];
            int32_t v;
            asm("smmulr %0, %1, %2" : "=r"(v) : "r"(ggS), "r"(t2));
            xq = z2 + v;
            z2 = xq + v;
        }

        // ---- stage 3 ----
        {
            int32_t u = xq - z3;
            asm("ssat %0, #27, %0" : "+r"(u));
            int32_t t2 = (u << 1) - filterDTable[(u + FILTER_BIAS) >> 16];
            int32_t v;
            asm("smmulr %0, %1, %2" : "=r"(v) : "r"(ggS), "r"(t2));
            xq = z3 + v;
            z3 = xq + v;
        }

        pre = xq << (FILTER_PRE_SHIFT - FILTER_FRAC);
  //*/


    }
gpio_put(TST_PIN,1);
//*/ 


                  pre *= *waveAmplGen;

                  *vb+=pre;
                  vb++;
                  *vb+=pre;
                  vb++;
            }

            filterG = filterGTarget;

            sinAmpl = sinTarget;                            // exact landing on the target
            triAmpl = triTarget;
            sawAmpl = sawTarget;
        }

        v->filterG = filterG;
        v->filter.z[0] = z0;  v->filter.z[1] = z1;  v->filter.z[2] = z2;  v->filter.z[3] = z3;
        
        *waveAmplSin = sinAmpl;
        *waveAmplTri = triAmpl;
        *waveAmplSaw = sawAmpl;           

        v->currEch    = currEch;
        v->currEchFra = currEchFra;

        v->noisePhase = nPhase;
        pink_state    = pink_state_loc;

      }

gpio_put(TST_PIN,0); 
restore_interrupts(ints);
}

void __not_in_flash_func(fillVoiceBuffer)(int32_t* vBuffer, Voice* voices, uint8_t bufNum)
{   
    blank_(vBuffer,SAMPLES_PER_BUFFER*2*4,0x00);   // env 12uS

    for(uint8_t v=0;v<MAX_VOICES;v++){   //MAX_VOICES;v++){
//gpio_put(TST_PIN,1);      
      fillVoiceBuffer_mono(vBuffer, &voices[v],v);
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
