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

/*#define FILTER_TANH_LEN  2049              // one entry per Q8 unit, covering -4..+4 directly, no interpolation needed
#define FILTER_TANH_HALF (FILTER_TANH_LEN / 2)
#define FILTER_KNEE       65536.0f      // raw units where the soft saturation starts (tanh argument = 1)
#define FILTER_BIAS (32768 + (FILTER_TANH_HALF << 16))   // rounding offset and table centre folded into one constant
#define FILTER_POLE_FACTOR 0.43498f      // sqrt(2^(1/4) - 1): -3 dB of 4 identical poles in cascade, in the tan domain (2 stages : 0,6436)*/

#define FILTER_FRAC       8             // fractional bits kept inside the filter (Q8)
#define FILTER_PRE_SHIFT  15            // shift between the voice signal and the filter input

//extern const int32_t filterDTable[];

/*//__attribute__((section(".ram_d1")))
int32_t filterDTable[FILTER_TANH_LEN];
//__attribute__((section(".ram_d1")))*/

//float gTable[FILTER_TABLE_LEN];

#define FILTER_RAMP_SHIFT 9                                    // log2(SAMPLES_PER_BUFFER): one coefficient ramp over the whole buffer
static_assert((1u << FILTER_RAMP_SHIFT) == SAMPLES_PER_BUFFER, "FILTER_RAMP_SHIFT must be log2(SAMPLES_PER_BUFFER)");
static int32_t voiceTmp[SAMPLES_PER_BUFFER];                   // mono signal of the voice being rendered, before the filter (voices run one after the other)

#define FILTER_RES_MAX 255                    // maximum resonance command: no damping in the second section


// -----

extern uint32_t millisCounter;

extern uint16_t amplLevel[];

// current lfo values (lfoHandler triger'd by pwmIrqHandler)
float       lfosFrequency[MAX_LFO];                   // frequency
uint16_t    lfosCodersFreq[MAX_LFO];                  // frequency coder value
uint16_t    lfosMaxCoderFreq[MAX_LFO];                // frequency coder max value
uint16_t    lfosCodersFreqAtt[MAX_LFO];               // frequency input attenuator value

//uint16_t    lfosStepInt[MAX_LFO];                     // partie entière du step lfo
//uint32_t    lfosStepFra[MAX_LFO];                     // partie fractionnaire du step lfo
volatile uint32_t lfosPhaseStep[MAX_LFO];       // Q16.16
uint32_t          lfoPhase[MAX_LFO];            // Q16.16, replaces currLfoEch's fraction

uint16_t    currLfoEch[MAX_LFO];
//uint32_t    currLfoEchFra[MAX_LFO];
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

int32_t     voicesScopeDataBuffer[MAX_VOICES*SAMPLES_PER_BUFFER];  // all voices data buffer 

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

/*void fillFilterTanH()
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
}

void fillFilterGTable()
{
    for (int i = 0; i < FILTER_TABLE_LEN; i++) {
        float oct  = (float)i / FILTER_STEPS_PER_OCT;
        float freq = FREQ0 * powf(2.0f, oct);
        gTable[i]  = tanf(M_PI * freq / SAMPLE_RATE) / FILTER_POLE_FACTOR;
    }
}

int32_t filterGTab[FILTER_CODE_MAX + 1];              // g = tan(pi*f/fs) in Q28, one entry per cutoff code, no interpolation (16 KB)

void fillFilterGTable()
{
    for (int c = 0; c <= FILTER_CODE_MAX; c++) {
        const float g = tanf((float)(M_PI * calcFreq((uint16_t)c) / SAMPLE_RATE));    // the oscillator's own frequency for this code
        filterGTab[c] = (int32_t)lroundf(g * 268435456.0f);
    }
}*/

int32_t filterGTab[FILTER_CODE_MAX + 1];              // g = tan(pi*f/fs) in Q28, one entry per cutoff code, no interpolation (16 KB)

void fillFilterGTable()
{
    for (int c = 0; c <= FILTER_CODE_MAX; c++) {
        const float g = tanf((float)(M_PI * calcFreq((uint16_t)c) / SAMPLE_RATE));    // the oscillator's own frequency for this code
        filterGTab[c] = (int32_t)lroundf(g * 268435456.0f);
    }
}

// position (0..1023) of the triangle peak in each half-table: it sets the width of the square ; computed once, at the first buffer
static uint16_t rcPeak[RC_N_TABLES];
static bool     rcPeakReady = false;

static void fillRcPeak()
{
    for (int t = 0; t < RC_N_TABLES; t++) {
        int16_t  best = rc_tables[t][0][WTRI];
        uint16_t pos  = 0;
        for (int i = 1; i < RC_N_SAMPLES; i++) {
            if (rc_tables[t][i][WTRI] > best) { best = rc_tables[t][i][WTRI]; pos = i; }
        }
        rcPeak[t] = pos;
    }
    rcPeakReady = true;
}

// initialisation des tableaux pour permettre calcFreq()
void sound_tables_init()        
{  
  printf("sound_tables_init\n");
  
  init_basic_sine_table();

  fillOctFreq();
  fillOctIncr();
  fillRcPeak();
  fillFilterGTable();                      // needs octFreq/octIncr: after fillOctIncr()
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

        voices[v].genAmpl=(int16_t)amplLevel[cga];
        voices[v].coderGenAmpl=cga;

        setVoiceFrCoders(&voices[v],coderF,FULL_ATTENUATION_VALUE);
        setVoiceFrequency(&voices[v]);

        voices[v].sampleNbToFill=SAMPLES_PER_BUFFER;    
        voices[v].currentSample=0;
        voices[v].phase=0;

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

        voices[v].sqrTh = 1024;
    }
}

void voicesInit(Voice* voices,float freq,uint8_t cga){
   voicesInit(voices,calcCoderFreq(freq),cga);
} 

void dumpVoices(Voice* v)
{
  printf("   frequency(c/M/f)   sampleNb currSample stepInt stepFra noisePhase noiseStep                                    WaveAmpl(c-M-b)                                             genAmpl(c=M=g)     switchs     \n");
  for(uint8_t n=0;n<MAX_VOICES;n++){  
 
    printf("%d %d-%d-%4.3f    %d       %d        %d      %d      %d        %d  ",n,v[n].coderFreq,VCES_MAX_FREQ_CODERS,v[n].frequency,v[n].sampleNbToFill,v[n].currentSample,v[n].phaseStep>>PHASE_SHIFT,v[n].phaseStep&((1u<<PHASE_SHIFT)-1),v[n].noisePhase,v[n].noiseStep);
    for(uint8_t wa=0;wa<BASIC_WAVES_NB;wa++){printf("%d-%d-%d ",v[n].coderWaveAmpl[wa],VCES_MAX_AMPL_CODERS,v[n].basicWaveAmpl[wa]);}
    printf("%d=%d=%d ",v[n].coderGenAmpl,VCES_MAX_AMPL_CODERS,v[n].genAmpl);

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

// base coder + attenuated control input, clamped to [lo,hi]: the one formula for every control input
static inline int32_t ctlCombine(int32_t coder,int32_t att,int16_t input,uint8_t inShift,int32_t lo,int32_t hi)
{
    int32_t v = coder + (((int32_t)(input >> inShift) * att) >> MAX_CTL_ATT_SHIFT);
    return (v < lo) ? lo : ((v > hi) ? hi : v);
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
void __not_in_flash_func(setVoiceSteps)(float freq,Voice* v){

    v->frequency=freq;                                              // display only

    float k=(float)BASIC_WAVE_TABLE_LEN*freq/SAMPLE_RATE;           // table steps per sample
    if (k > 2047.0f) { k = 2047.0f; }                               // 2047 * 2^21 is the largest step a 32-bit phase can hold
    v->phaseStep=(uint32_t)(k*(float)(1UL<<PHASE_SHIFT)+0.5f);      // k * 2^21 < 2^32, one 32-bit store 
}

void __not_in_flash_func(setVoiceFrCoders)(Voice* v, uint16_t coderFreq, uint16_t coderAtt)  // coders storage
{
    v->coderFreq = coderFreq;
    v->coderFreqAtt = coderAtt;
}    

void __not_in_flash_func(setVoiceFrequency)(Voice* v)  // recompute the frequency with the stored input value
{
    int16_t id = v->voice_ctl_input_id[VFRQ];
    float f=calcFreq((uint16_t)ctlCombine(v->coderFreq,v->coderFreqAtt,ctl_input_val[id],6,0,FREQ_CODE_MAX));
    setVoiceSteps(f,v);                
}

void __not_in_flash_func(setVoiceCycleR)(Voice* v, uint8_t coderCycleR, int16_t coderCycleRAtt)
{
    v->coderCycleR    = coderCycleR;
    v->coderCycleRAtt = coderCycleRAtt;

    int16_t id = v->voice_ctl_input_id[VCR_];
    update_inputs(id, ctl_input_val[id]);                // recompute cycleR from the stored input value
}

// *************************** filters ****************************

//const float FILTER_MAX_FREQ = FREQ0 * 1024.0f;    // FREQ0 * 2^FILTER_MAX_OCT, reste sous Nyquist
//const float FILTER_MIN_FREQ = 20.0f;

/*void filtersInit(uint8_t v)
{
    setVoiceFilter(&voices[v], (int16_t)(FILTER_MAX_OCT * octIncrNb), 0, MAX_CTL_ATT, 0, MAX_CTL_ATT);   // cutoff grand ouvert par défaut ; atten wide open
    //voices[v].filterG      = voices[v].newFilterG;
    //voices[v].filterStages = 4;
    memset(voices[v].filter.z, 0, sizeof(voices[v].filter.z));
}*/

#define FILTER_INPUT_SHIFT 6        // use the value the old VFIL case of update_inputs had

void __not_in_flash_func(setVoiceFilterFrequency)(Voice* v)     // recompute the cutoff code with the stored input value
{
    int16_t id = v->voice_ctl_input_id[VFIL];
    int32_t code = ctlCombine(v->coderFilterFreq, v->coderFilterFreqAtt, ctl_input_val[id], FILTER_INPUT_SHIFT, 0, FILTER_CODE_MAX);
    v->filterFrequency = calcFilterFreq(code);                  // display: now includes the modulation
    v->filterCode = (int16_t)code;                              // one 16-bit store, read once per sub-block
}

void __not_in_flash_func(setVoiceFilterRes)(Voice* v)
{
    int16_t id = v->voice_ctl_input_id[VRES];
    v->filterRes = (uint8_t)ctlCombine(v->coderFilterRes, v->coderFilterResAtt, ctl_input_val[id], 7, 0, FILTER_RES_MAX);   // a full-scale input at full attenuator sweeps the whole range
}

void __not_in_flash_func(setVoiceFilter)(Voice* v, int16_t coderFilterF, int16_t coderFilterFAtt, int16_t coderFilterLevAtt, int16_t coderFilterRes, int16_t coderFilterResAtt)
{
    v->coderFilterFreq    = coderFilterF;
    v->coderFilterFreqAtt = coderFilterFAtt;
    v->coderFilterLevAtt  = coderFilterLevAtt;
    v->coderFilterRes     = coderFilterRes;
    v->coderFilterResAtt  = coderFilterResAtt;

    int16_t id = v->voice_ctl_input_id[VFIL];
    update_inputs(id, ctl_input_val[id]);                       // recomputes the cutoff code (update_inputs calls setVoiceFilterFrequency)
    setVoiceFilterRes(v);
}

void filtersInit(uint8_t v)
{
    setVoiceFilter(&voices[v], (int16_t)FILTER_CODE_MAX, 0, MAX_CTL_ATT, 0, 0);   // cutoff wide open, no resonance, unity input gain
    memset(voices[v].filter.z, 0, sizeof(voices[v].filter.z));
}

float __not_in_flash_func(calcFilterFreq)(int32_t code)                         // cutoff in Hz for a filter code, same domain and limits as calcFilterG
{
    if (code < 0) code = 0;
    if (code > FILTER_MAX_OCT * octIncrNb) code = FILTER_MAX_OCT * octIncrNb;   // the filter stops at ~16.7 kHz
    return calcFreq((uint16_t)code);
}

/*void __not_in_flash_func(setFilterFrequency)(float freqHz, Voice* v)
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

void __not_in_flash_func(setVoiceFilter)(Voice* v, int16_t coderFilterF, int16_t coderFilterFAtt, int16_t coderFilterLevAtt, int16_t coderFilterRes, int16_t coderFilterResAtt)
{
    v->coderFilterFreq    = coderFilterF;
    v->coderFilterFreqAtt = coderFilterFAtt;
    v->coderFilterLevAtt  = coderFilterLevAtt;
    v->coderFilterRes     = coderFilterRes;
    v->coderFilterResAtt  = coderFilterResAtt;

    v->filterFrequency = calcFilterFreq(coderFilterF);     // display only: cutoff set by the coder (modulation not included)

    int16_t id = v->voice_ctl_input_id[VFIL];    
    update_inputs(id, ctl_input_val[id]);
}*/

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

static inline __attribute__((always_inline)) int32_t smmlar(int32_t a, int32_t b, int32_t acc)   // acc + high word of a*b, rounded: one instruction
{
    int32_t r;
    asm("smmlar %0, %1, %2, %3" : "=r"(r) : "r"(a), "r"(b), "r"(acc));
    return r;
}

static inline __attribute__((always_inline)) int32_t ssat29(int32_t x)    // clamp to +-2^28
{
    int32_t r;
    asm("ssat %0, #29, %1" : "=r"(r) : "r"(x));
    return r;
}

static inline __attribute__((always_inline)) int32_t ssat24(int32_t x)    // clamp to +-2^23: a full-scale voice
{
    int32_t r;
    asm("ssat %0, #24, %1" : "=r"(r) : "r"(x));
    return r;
}

#define FILTER_K1  1.8477591f      // 1/Q1 = 2*sin(3*pi/8), first section (Q = 0.5412)
#define FILTER_K2  0.7653669f      // 1/Q2 = 2*sin(pi/8), second section without resonance (Q = 1.3066)

// coefficients, computed once per sub-block in float: c[0..2] section 1 (a1,a2,a3), c[3..5] section 2, all Q31
static inline void filterCoefs(int32_t gQ28, int32_t res, int32_t* c)
{
    const float g  = (float)gQ28 * (1.0f / 268435456.0f);
    const float k2 = FILTER_K2 * (float)(FILTER_RES_MAX - res) * (1.0f / FILTER_RES_MAX);   // damping of section 2: 0 at the maximum command

    float a1 = 1.0f / (1.0f + g * (g + FILTER_K1));
    float a2 = g * a1;
    float a3 = g * a2;
    c[0] = (int32_t)(a1 * 2147483648.0f);
    c[1] = (int32_t)(a2 * 2147483648.0f);
    c[2] = (int32_t)(a3 * 2147483648.0f);

    a1 = 1.0f / (1.0f + g * (g + k2));
    a2 = g * a1;
    a3 = g * a2;
    c[3] = (int32_t)(a1 * 2147483648.0f);
    c[4] = (int32_t)(a2 * 2147483648.0f);
    c[5] = (int32_t)(a3 * 2147483648.0f);
}

// one state-variable section, integer, 4 high-word multiplies
// x, ic1, ic2, result in Q8 like the voice signal ; a1,a2,a3 in Q31 (the high-word products are halved, hence the shifts)
static inline __attribute__((always_inline)) int32_t filterSection(int32_t& ic1, int32_t& ic2, int32_t x, int32_t a1, int32_t a2, int32_t a3)
{
    const int32_t v3  = x - ic2;
    const int32_t t13 = smmlar(a2, v3, smmulr(a1, ic1));       // (a1*ic1 + a2*v3) / 2
    const int32_t t24 = smmlar(a3, v3, smmulr(a2, ic1));       // (a2*ic1 + a3*v3) / 2
    const int32_t y   = ic2 + (t24 << 1);                      // low-pass output
    ic1 = (t13 << 2) - ic1;
    ic2 = ic2 + (t24 << 2);
    return y;
}

// same, with the two states limited to +-2^28 so that nothing wraps around when the resonance amplifies a loud signal
static inline __attribute__((always_inline)) int32_t filterSectionLim(int32_t& ic1, int32_t& ic2, int32_t x, int32_t a1, int32_t a2, int32_t a3)
{
    const int32_t v3  = x - ic2;
    const int32_t t13 = smmlar(a2, v3, smmulr(a1, ic1));
    const int32_t t24 = smmlar(a3, v3, smmulr(a2, ic1));
    const int32_t y   = ic2 + (t24 << 1);
    ic1 = ssat29((t13 << 2) - ic1);
    ic2 = ssat29(ic2 + (t24 << 2));
    return y;
}

// *************************** lfos ****************************

void __not_in_flash_func(setLfosSteps)(float freq,uint8_t lfo){

    lfosFrequency[lfo]=freq;

    float k=(float)BASIC_WAVE_TABLE_LEN*freq/LFOS_SAMPLE_RATE;
    lfosPhaseStep[lfo]=(uint32_t)(k*(float)MAX_STEP_FRA+0.5f);
}

void __not_in_flash_func(setLfosFrCoders)(uint8_t lfo, uint16_t coderFreq, uint16_t coderAtt)   // coders storage only
{
    lfosCodersFreq[lfo]    = coderFreq;
    lfosCodersFreqAtt[lfo] = coderAtt;
}

void __not_in_flash_func(setLfosFrequency)(uint8_t lfo)  // recompute the frequency with the stored input value
{
    int16_t id = lfo_ctl_input_id[lfo][LFRQ];
    float f=calcFreq((uint16_t)ctlCombine(lfosCodersFreq[lfo],lfosCodersFreqAtt[lfo],ctl_input_val[id],3,0,FREQ_CODE_MAX))/VOICE_FREQ_DIVIDER;
    setLfosSteps(f,lfo);                
}

void __not_in_flash_func(setLfosCycleR)(uint8_t lfo, uint16_t coderCycleR, uint16_t coderCycleRAtt)
{
    lfosCoderCycleR[lfo]    = coderCycleR;
    lfosCoderCycleRAtt[lfo] = coderCycleRAtt;

    int16_t id = lfo_ctl_input_id[lfo][LCR_];
    update_inputs(id, ctl_input_val[id]);                // recompute lfosCycleR from the stored input value
}

void lfosInit(){
    printf("%u lfos init\n",MAX_LFO);
    for(uint8_t l=0;l<MAX_LFO;l++){

        lfosCodersFreq[l]=1768;    // 1.5s
        lfosFrequency[l]=calcFreq(lfosCodersFreq[l])/VOICE_FREQ_DIVIDER;
        lfosMaxCoderFreq[l]=LFOS_MAX_FREQ_CODERS;
        lfosCodersFreqAtt[l]=FULL_ATTENUATION_VALUE;
        currLfoEch[l]=0;
        lfosPhaseStep[l]=0; 
        lfoPhase[l]=0;
        
        for(uint8_t v=0;v<MAX_OUTPUTS_PER_OBJ;v++){lfosOutputsValues[l][v]=0;}

        setLfosSteps(lfosFrequency[l],l);
        setLfosCycleR(l, MAXCODER_RC/2, FULL_ATTENUATION_VALUE);        

        lfoTime=0;
    }
    memset(lfoScopeBuffer,0x0000,sizeof(lfoScopeBuffer));

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

        uint32_t rc = lfosCycleR[l]&63;                 // rc=0-63
        uint32_t rcTableNb = (rc <= 32) ? rc : (64 - rc);

        uint32_t ph = (lfoPhase[l] + lfosPhaseStep[l]) & LFO_PHASE_MASK;
        lfoPhase[l] = ph;
        uint32_t ce = ph >> LFO_PHASE_SHIFT;            // table index before the half-wave mirroring below
        currLfoEch[l]=ce;                               // long term value (the square wave reads it)

        bool vv=(ce<RC_TABLES_LEN);
        int sign=(vv*2-1);                              // invert 180-360°

        ce ^= (!vv) * (BASIC_WAVE_TABLE_LEN - 1);       // ce = vv*ce+!vv*((BASIC_WAVE_TABLE_LEN-1) - currEch);  // invert 180-360°       
        ce &= RC_TABLES_LEN-1;

        uint32_t ce_idx = ce;
        if (rc > (RC_N_TABLES-1)){ce_idx = (RC_TABLES_LEN - 1) - ce_idx;}
        
        const int16_t *w = &rc_tables[rcTableNb][0][0]+RC_N_WAVES*ce_idx;    // rc_table values ptr 
        
        int16_t c0=sign*w[LSIN];
        int16_t out_id;
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
      }

    lfoScopeBufPtr++;
    lfoScopeBufPtr&=OSC_SCOPE_BUFFER_LEN-1;
  }
}

// ***************************  voices producer  ******************************

/*// 
void __not_in_flash_func(fillVoiceBuffer_mono)(volatile int32_t* vBuffer,Voice* v,uint8_t voiceNum)
{   

uint32_t ints = save_and_disable_interrupts();
gpio_put(TST_PIN,1);
      
    i2s_buf_scope=vBuffer;

// init noise
    uint32_t nPhase        = v->noisePhase;
    const uint32_t nStep   = v->noiseStep;
    int32_t pink_state_loc = pink_state;
    uint32_t limit = (uint32_t)NOISE_TABLE_SIZE << 16;

// init rc
    uint32_t rc = v->cycleR&63;                   // rc=0-63
    uint32_t rcTableNb = (rc <= 32) ? rc : (64 - rc);
    bool     reverseCeIdx = (rc > 32);            // was tested every sample as "if (rc > 32)"
    int      sawRcSign    = (rc >= 32) ? -1 : 1;  // was tested every sample as "if(rc>=32){saw=-saw;}"

    int16_t* rcTableCurr = &rc_tables[rcTableNb][0][0];
    int16_t* rcTable32 = &rc_tables[32][0][0];                           // base table 32 pour saw 
    
// init sqr : width, minimum width (4 samples and SQR_MIN_WIDTH), levels with zero mean
#define SQR_MIN_WIDTH 205 
                                   // 10 % of the period (2048 table units); 0 = no limit
    int32_t sqrTh  = 2 * (reverseCeIdx ? (1023 - rcPeak[rcTableNb]) : rcPeak[rcTableNb]);
    int32_t thMin  = 4 * (int32_t)(v->phaseStep >> PHASE_SHIFT);     // 4 samples wide at least
    if (thMin < SQR_MIN_WIDTH) thMin = SQR_MIN_WIDTH;
    if (thMin >= 1024) { sqrTh = 1024; }                              // high pitch: the square stays symmetric
    else { if (sqrTh < thMin) sqrTh = thMin;  if (sqrTh > 2048 - thMin) sqrTh = 2048 - thMin; }
    const float   sqrD  = (float)sqrTh * (1.0f / 2048.0f);
    const float   sqrK  = 32767.0f / ((sqrD > 0.5f) ? sqrD : (1.0f - sqrD));
    const int32_t sqrHi =  (int32_t)((1.0f - sqrD) * sqrK);          // zero mean: hi*d + lo*(1-d) = 0
    const int32_t sqrLo = -(int32_t)(sqrD * sqrK);    

// init waves ampl (pointers needed for real time changes)     
    volatile int16_t* waveAmplSin    = &v->basicWaveAmpl[WSIN];
    volatile int16_t* newWaveAmplSin = &v->newBasicWaveAmpl[WSIN];
    volatile int16_t* waveAmplTri    = &v->basicWaveAmpl[WTRI];
    volatile int16_t* newWaveAmplTri = &v->newBasicWaveAmpl[WTRI];      
    volatile int16_t* waveAmplSaw    = &v->basicWaveAmpl[WSAW];
    volatile int16_t* newWaveAmplSaw = &v->newBasicWaveAmpl[WSAW];          
    volatile int16_t* waveAmplSqr    = &v->basicWaveAmpl[WSQR]; 
    volatile int16_t* newWaveAmplSqr = &v->newBasicWaveAmpl[WSQR];
    volatile int16_t* waveAmplWhi    = &v->basicWaveAmpl[WHIT];
    volatile int16_t* waveAmplPnk    = &v->basicWaveAmpl[PONK];

    volatile int16_t* waveAmplGen    = &v->genAmpl;

    {
        //volatile int32_t* vb=vBuffer;

  // fast loop computing the sample indices

        uint32_t       phase     = v->phase;                            // replaces currEch/currEchFra
        const uint32_t phaseStep = v->phaseStep;
        int32_t* vsBuffer = voicesScopeDataBuffer + voiceNum*SAMPLES_PER_BUFFER;
        for (uint32_t i = 0; i < SAMPLES_PER_BUFFER; i += 4) {          // unrolled x4: the loop overhead is paid once per 4 samples
            phase += phaseStep; vsBuffer[i]   = phase >> PHASE_SHIFT;
            phase += phaseStep; vsBuffer[i+1] = phase >> PHASE_SHIFT;
            phase += phaseStep; vsBuffer[i+2] = phase >> PHASE_SHIFT;
            phase += phaseStep; vsBuffer[i+3] = phase >> PHASE_SHIFT;
        }
        v->phase = phase;      

  // waves gen (filling i2s data)
        #define RAMP_SHIFT  7                                             // 128-sample sub-blocks = 2.9 ms at 44.1 kHz
        #define RAMP_LEN   (1u << RAMP_SHIFT)                             
        static_assert((SAMPLES_PER_BUFFER % RAMP_LEN) == 0, "SAMPLES_PER_BUFFER must be a multiple of RAMP_LEN");

        int32_t sinAmpl = *waveAmplSin;
        int32_t triAmpl = *waveAmplTri;
        int32_t sawAmpl = *waveAmplSaw;
      
        for (uint32_t sb = 0; sb < SAMPLES_PER_BUFFER; sb += RAMP_LEN)    // NEW: outer loop, one pass per sub-block
        {
            // anti-clic
            int32_t       sinTarget = *newWaveAmplSin;                    // NEW: read the target once per sub-block
            int32_t       sinInc    = sinTarget - sinAmpl;                // NEW: gap between target and current amplitude
            int32_t       sinAcc    = sinAmpl << RAMP_SHIFT;              // NEW: current amplitude times 128

            int32_t       triTarget = *newWaveAmplTri;                    // NEW: read the target once per sub-block
            int32_t       triInc    = triTarget - triAmpl;                // NEW: gap between target and current amplitude
            int32_t       triAcc    = triAmpl << RAMP_SHIFT;              // NEW: current amplitude times 128

            int32_t       sawTarget = *newWaveAmplSaw;                    // NEW: read the target once per sub-block
            int32_t       sawInc    = sawTarget - sawAmpl;                // NEW: gap between target and current amplitude
            int32_t       sawAcc    = sawAmpl << RAMP_SHIFT;              // NEW: current amplitude times 128   
            
            // muted-wave flags, evaluated once per sub-block
            bool noiseOn = (*waveAmplWhi != 0) || (*waveAmplPnk != 0);
            
            bool wsinOn  = ((sinAmpl | sinTarget) != 0);     // running amplitude or target: the ramp must be able to start and to finish
            bool wtriOn  = ((triAmpl | triTarget) != 0);
            bool wsawOn  = ((sawAmpl | sawTarget) != 0);
            bool wsqrOn  = (*newWaveAmplSqr != 0);           // the square has no ramp: test the value that is used

            for(uint32_t s = sb; s < sb + RAMP_LEN; s++)
            {
                  // !!!!! pour le scope un buffer séparé serait utile : !!!!! 
                  // le scope affiche lentement et i2sbuf est modifié rapidement 

                  // waves

                  uint32_t ce=vsBuffer[s];
                  const uint32_t t = ce;  

                  bool vv=(ce<RC_TABLES_LEN);
                  int sign=(vv*2-1);                                // invert 180-360°

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

                    sawAcc += sawInc;
                    pre += rc_tables[0][ce][WTRI] * (sawAcc >> RAMP_SHIFT);     // sawtooth = triangle of table 0, the same for every rc         
                  }          

                  //if (__builtin_expect((wsqrOn),false)){
                  //  int16_t sqr=(ce & (BASIC_WAVE_TABLE_LEN>>1)) ? -0x7fff : 0x7fff;    // sqr cr to be implemented
                  //  pre += (sqr * *newWaveAmplSqr); 
                  //}

                  pre *= sign;

                  if (__builtin_expect((wsqrOn),false)){                                  // must be "after" sign
                    pre += ((t < (uint32_t)sqrTh) ? sqrHi : sqrLo) * *newWaveAmplSqr;     // compared with the raw phase: no previous value needed
                  }
  
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

                voiceTmp[s] = pre;                            // filter, gain and mix are done after this loop
            }

            sinAmpl = sinTarget;                            // exact landing on the target
            triAmpl = triTarget;
            sawAmpl = sawTarget;
        }

        // filter: two state-variable sections in cascade (Butterworth 4th order), coefficients refreshed every sub-block

        int32_t s0 = v->filter.z[0], s1 = v->filter.z[1], s2 = v->filter.z[2], s3 = v->filter.z[3];    // integrator states of section 1 and section 2
        int32_t levAtt = v->coderFilterLevAtt;
        if (levAtt > MAX_CTL_ATT) levAtt = MAX_CTL_ATT;
        if (levAtt < 0)           levAtt = 0;
        const int32_t attQ22 = levAtt << 22;           // 256 << 22 = 2^30: unity gain fits in a signed 32-bit word
        const int32_t gen    = *waveAmplGen;           // voice gain, read once per buffer

        volatile int32_t* vb = vBuffer;
        for (uint32_t sb = 0; sb < SAMPLES_PER_BUFFER; sb += RAMP_LEN) {
            int32_t c[6];
            filterCoefs(filterGTab[v->filterCode], v->filterRes, c);       // control values read once per sub-block

            for (uint32_t i = sb; i < sb + RAMP_LEN; i++) {                // section 1, with the input attenuator, in place
                const int32_t xq = smmulr(voiceTmp[i] >> (FILTER_PRE_SHIFT - FILTER_FRAC), attQ22) * 4;    // xq = input * att / 256
                voiceTmp[i] = filterSection(s0, s1, xq, c[0], c[1], c[2]);
            }
            for (uint32_t i = sb; i < sb + RAMP_LEN; i++) {                // section 2 (resonant), voice gain, mix into the output (left = right)
                const int32_t y   = ssat24(filterSectionLim(s2, s3, voiceTmp[i], c[3], c[4], c[5]));    // never more than a full-scale voice
                const int32_t out = (y * (1 << (FILTER_PRE_SHIFT - FILTER_FRAC))) * gen;
                vb[0] += out;
                vb[1] += out;
                vb += 2;
            }
        }

        v->filter.z[0] = s0;  v->filter.z[1] = s1;  v->filter.z[2] = s2;  v->filter.z[3] = s3;
      
        *waveAmplSin = sinAmpl;
        *waveAmplTri = triAmpl;
        *waveAmplSaw = sawAmpl;           

        //v->currEch    = currEch;
        //v->currEchFra = currEchFra;
        //v->currEch    = phase >> 16;
        //v->currEchFra = phase & (MAX_STEP_FRA - 1);

        v->noisePhase = nPhase;
        pink_state    = pink_state_loc;

    }

gpio_put(TST_PIN,0); 
restore_interrupts(ints);
}*/

// ***************************  voices producer  ******************************

#define RAMP_SHIFT  7                                   // 128-sample sub-blocks = 2.9 ms at 44.1 kHz
#define RAMP_LEN   (1u << RAMP_SHIFT)
static_assert((SAMPLES_PER_BUFFER % RAMP_LEN) == 0, "SAMPLES_PER_BUFFER must be a multiple of RAMP_LEN");

#define SQR_MIN_WIDTH 205                               // minimum width of the square pulse: 10 % of the period (2048 table units); 0 = no limit

// everything one sub-block of the oscillator needs; filled by fillVoiceBuffer_mono, once per sub-block
struct OscBlock {
    const int32_t* idx;                 // raw table indices of the sub-block (vsBuffer + sb)
    int32_t*       out;                 // voiceTmp + sb
    const int16_t* rcCurr;              // rc table of the voice
    bool           reverse;             // rc > 32
    int32_t        sinAcc, sinInc;      // amplitude ramps: current value << RAMP_SHIFT, and step per sample
    int32_t        triAcc, triInc;
    int32_t        sawAcc, sawInc;
    uint32_t       sqrTh;               // width of the square pulse, in table units (0..2048)
    int32_t        sqrAHi, sqrALo;      // square levels, already multiplied by the square amplitude
    int32_t        whiAmpl, pnkAmpl;
    uint32_t       seed;                // noise generator state (xorshift32)
    int32_t        pink;
};

// one sub-block of the oscillator, specialised at compile time for the waves that are active:
// no per-sample test, and the variables of the muted waves do not exist in the loop
template<bool kTri, bool kSaw, bool kSqr, bool kWhite, bool kPink>
static inline __attribute__((always_inline)) void oscBlock(OscBlock& b)
{
    const int32_t* idx    = b.idx;
    int32_t*       out    = b.out;
    const int16_t* rcCurr = b.rcCurr;
    const bool     rev    = b.reverse;
    int32_t sinAcc = b.sinAcc;  const int32_t sinInc = b.sinInc;
    int32_t triAcc = b.triAcc;  const int32_t triInc = b.triInc;
    int32_t sawAcc = b.sawAcc;  const int32_t sawInc = b.sawInc;
    const uint32_t sqrTh = b.sqrTh;  const int32_t sqrAHi = b.sqrAHi, sqrALo = b.sqrALo;
    const int32_t whiAmpl = b.whiAmpl, pnkAmpl = b.pnkAmpl;
    uint32_t seed = b.seed;
    int32_t  pink = b.pink;

    for (uint32_t s = 0; s < RAMP_LEN; s++) {
        const uint32_t t = idx[s];                                     // raw phase index 0..2047

        uint32_t ce = t;
        const bool vv = (ce < RC_TABLES_LEN);
        const int  sign = (vv * 2 - 1);                                // invert 180-360°
        ce ^= (!vv) * (BASIC_WAVE_TABLE_LEN - 1);
        ce &= RC_TABLES_LEN - 1;

        uint32_t ce_idx = ce;
        if (rev) ce_idx = (RC_TABLES_LEN - 1) - ce_idx;
        const int16_t* w = rcCurr + RC_N_WAVES * ce_idx;               // rc_table values ptr

        sinAcc += sinInc;
        int32_t pre = w[WSIN] * (sinAcc >> RAMP_SHIFT);

        if (kTri) {
            triAcc += triInc;
            pre += w[WTRI] * (triAcc >> RAMP_SHIFT);
        }
        if (kSaw) {
            sawAcc += sawInc;
            pre += rc_tables[0][ce][WTRI] * (sawAcc >> RAMP_SHIFT);    // sawtooth = triangle of table 0, the same for every rc
        }

        pre *= sign;

        if (kSqr) {
            pre += (t < sqrTh) ? sqrAHi : sqrALo;                      // compared with the raw phase, added after the sign
        }

        if (kWhite || kPink) {
            seed ^= seed << 13;  seed ^= seed >> 17;  seed ^= seed << 5;          // xorshift32: a new random number at every sample, no repetition
            const int32_t white = (int16_t)(seed >> 16);
            if (kWhite) pre += white * whiAmpl;
            if (kPink) {
                pink = (alpha * pink + (32768 - alpha) * white) >> 15; // one-pole low-pass of the white noise
                pre += pink * pnkAmpl;
            }
        }

        out[s] = pre;
    }

    b.seed   = seed;                                                   // the noise state continues in the next sub-block
    b.pink   = pink;
}

// the 5 flags (triangle, saw, square, white noise, pink noise) as the 5 bits of a mask: 32 specialised loops
template<int M>
static inline __attribute__((always_inline)) void oscMask(OscBlock& b)
{
    oscBlock<(M & 1) != 0, (M & 2) != 0, (M & 4) != 0, (M & 8) != 0, (M & 16) != 0>(b);
}

#define OSC_CASE(n)  case n: oscMask<n>(b); break;
#define OSC_DISPATCH(mask)                                                                         \
    switch (mask) {                                                                                \
        OSC_CASE( 0) OSC_CASE( 1) OSC_CASE( 2) OSC_CASE( 3) OSC_CASE( 4) OSC_CASE( 5)              \
        OSC_CASE( 6) OSC_CASE( 7) OSC_CASE( 8) OSC_CASE( 9) OSC_CASE(10) OSC_CASE(11)              \
        OSC_CASE(12) OSC_CASE(13) OSC_CASE(14) OSC_CASE(15) OSC_CASE(16) OSC_CASE(17)              \
        OSC_CASE(18) OSC_CASE(19) OSC_CASE(20) OSC_CASE(21) OSC_CASE(22) OSC_CASE(23)              \
        OSC_CASE(24) OSC_CASE(25) OSC_CASE(26) OSC_CASE(27) OSC_CASE(28) OSC_CASE(29)              \
        OSC_CASE(30) OSC_CASE(31)                                                                  \
        default: break;                                                                            \
    }

void __not_in_flash_func(fillVoiceBuffer_mono)(volatile int32_t* vBuffer,Voice* v,uint8_t voiceNum)
{
    if (__builtin_expect(!rcPeakReady, 0)) { fillRcPeak(); }              // first buffer only, before the interrupts are masked

    uint32_t ints = save_and_disable_interrupts();
    gpio_put(TST_PIN,1);

    i2s_buf_scope=vBuffer;

    const uint32_t phaseStep = v->phaseStep;                              // read once for the whole buffer

// init noise: one xorshift32 generator per voice (its state must never be 0); v->noisePhase holds the state
    uint32_t nSeed = v->noisePhase;
    if (nSeed == 0) { nSeed = 0x9E3779B9u + 0x7F4A7C15u * (uint32_t)(voiceNum + 1); }
    int32_t  pink_state_loc = pink_state;

// init rc
    const uint32_t rc           = v->cycleR & 63;                         // rc=0-63
    const uint32_t rcTableNb    = (rc <= 32) ? rc : (64 - rc);
    const bool     reverseCeIdx = (rc > 32);                              // was tested every sample as "if (rc > 32)"
    const int16_t* rcTableCurr  = &rc_tables[rcTableNb][0][0];

// square: target width in table units (0..2048) = 2 x position of the triangle peak, with a minimum width
    int32_t sqrThTarget = 2 * (reverseCeIdx ? (1023 - rcPeak[rcTableNb]) : rcPeak[rcTableNb]);
    int32_t thMin       = 4 * (int32_t)(phaseStep >> PHASE_SHIFT);        // at least 4 samples wide
    if (thMin < SQR_MIN_WIDTH) thMin = SQR_MIN_WIDTH;
    if (thMin >= 1024) { sqrThTarget = 1024; }                            // high pitch: the square stays symmetric
    else {
        if (sqrThTarget < thMin)        sqrThTarget = thMin;
        if (sqrThTarget > 2048 - thMin) sqrThTarget = 2048 - thMin;
    }
    int32_t sqrTh = v->sqrTh;                                             // current width: it glides towards sqrThTarget

// init waves ampl (pointers needed for real time changes)
    volatile int16_t* waveAmplSin    = &v->basicWaveAmpl[WSIN];
    volatile int16_t* newWaveAmplSin = &v->newBasicWaveAmpl[WSIN];
    volatile int16_t* waveAmplTri    = &v->basicWaveAmpl[WTRI];
    volatile int16_t* newWaveAmplTri = &v->newBasicWaveAmpl[WTRI];
    volatile int16_t* waveAmplSaw    = &v->basicWaveAmpl[WSAW];
    volatile int16_t* newWaveAmplSaw = &v->newBasicWaveAmpl[WSAW];
    volatile int16_t* newWaveAmplSqr = &v->newBasicWaveAmpl[WSQR];
    volatile int16_t* newWaveAmplWhi = &v->newBasicWaveAmpl[WHIT];       // noises have no ramp: the target is the value that is used
    volatile int16_t* newWaveAmplPnk = &v->newBasicWaveAmpl[PONK];
    volatile int16_t* waveAmplGen    = &v->genAmpl;

// fast loop computing the sample indices (unrolled x4: the loop overhead is paid once per 4 samples)
    uint32_t phase = v->phase;
    int32_t* vsBuffer = voicesScopeDataBuffer + voiceNum*SAMPLES_PER_BUFFER;
    for (uint32_t i = 0; i < SAMPLES_PER_BUFFER; i += 4) {
        phase += phaseStep; vsBuffer[i]   = phase >> PHASE_SHIFT;         // table index 0-2047
        phase += phaseStep; vsBuffer[i+1] = phase >> PHASE_SHIFT;
        phase += phaseStep; vsBuffer[i+2] = phase >> PHASE_SHIFT;
        phase += phaseStep; vsBuffer[i+3] = phase >> PHASE_SHIFT;
    }
    v->phase = phase;

// oscillator: waves and noise into voiceTmp, one sub-block at a time
    int32_t sinAmpl = *waveAmplSin;
    int32_t triAmpl = *waveAmplTri;
    int32_t sawAmpl = *waveAmplSaw;

    for (uint32_t sb = 0; sb < SAMPLES_PER_BUFFER; sb += RAMP_LEN)        // one pass per sub-block
    {
        // anti-clic: amplitude ramps
        const int32_t sinTarget = *newWaveAmplSin;                        // target read once per sub-block
        const int32_t sinInc    = sinTarget - sinAmpl;                    // gap between target and current amplitude
        const int32_t sinAcc    = sinAmpl << RAMP_SHIFT;                  // current amplitude times 128

        const int32_t triTarget = *newWaveAmplTri;
        const int32_t triInc    = triTarget - triAmpl;
        const int32_t triAcc    = triAmpl << RAMP_SHIFT;

        const int32_t sawTarget = *newWaveAmplSaw;
        const int32_t sawInc    = sawTarget - sawAmpl;
        const int32_t sawAcc    = sawAmpl << RAMP_SHIFT;

        // muted-wave flags, evaluated once per sub-block
        const bool whiteOn = (*newWaveAmplWhi != 0);
        const bool pinkOn  = (*newWaveAmplPnk != 0);
        const bool wtriOn  = ((triAmpl | triTarget) != 0);                // running amplitude or target: the ramp must be able to start and to finish
        const bool wsawOn  = ((sawAmpl | sawTarget) != 0);
        const bool wsqrOn  = (*newWaveAmplSqr != 0);                      // the square has no ramp: test the value that is used

        OscBlock b;
        b.idx = vsBuffer + sb;   b.out = voiceTmp + sb;
        b.rcCurr = rcTableCurr;  b.reverse = reverseCeIdx;
        b.sinAcc = sinAcc;  b.sinInc = sinInc;
        b.triAcc = triAcc;  b.triInc = triInc;
        b.sawAcc = sawAcc;  b.sawInc = sawInc;
        b.sqrTh = 0;  b.sqrAHi = 0;  b.sqrALo = 0;
        if (wsqrOn) {
            sqrTh += (sqrThTarget - sqrTh) >> 1;                          // glide: half of the remaining distance per sub-block
            const float   sqrD = (float)sqrTh * (1.0f / 2048.0f);
            const float   sqrK = 32767.0f / ((sqrD > 0.5f) ? sqrD : (1.0f - sqrD));
            const int32_t amp  = *newWaveAmplSqr;
            b.sqrTh  = (uint32_t)sqrTh;
            b.sqrAHi =  (int32_t)((1.0f - sqrD) * sqrK) * amp;            // zero-mean levels, multiplied by the amplitude once per sub-block
            b.sqrALo = -(int32_t)(sqrD * sqrK) * amp;
        }
        b.whiAmpl = *newWaveAmplWhi;  b.pnkAmpl = *newWaveAmplPnk;
        b.seed = nSeed;  b.pink = pink_state_loc;

        OSC_DISPATCH((wtriOn ? 1 : 0) | (wsawOn ? 2 : 0) | (wsqrOn ? 4 : 0) | (whiteOn ? 8 : 0) | (pinkOn ? 16 : 0))

        nSeed = b.seed;  pink_state_loc = b.pink;

        sinAmpl = sinTarget;                                              // exact landing on the target
        triAmpl = triTarget;
        sawAmpl = sawTarget;
    }

// filter: two state-variable sections in cascade (Butterworth 4th order), coefficients refreshed every sub-block
    int32_t s0 = v->filter.z[0], s1 = v->filter.z[1], s2 = v->filter.z[2], s3 = v->filter.z[3];    // integrator states of section 1 and section 2
    int32_t levAtt = v->coderFilterLevAtt;
    if (levAtt > MAX_CTL_ATT) levAtt = MAX_CTL_ATT;
    if (levAtt < 0)           levAtt = 0;
    const int32_t attQ22 = levAtt << 22;                                  // 256 << 22 = 2^30: unity gain fits in a signed 32-bit word
    const int32_t gen    = *waveAmplGen;                                  // voice gain, read once per buffer

    volatile int32_t* vb = vBuffer;
    for (uint32_t sb = 0; sb < SAMPLES_PER_BUFFER; sb += RAMP_LEN) {
        int32_t c[6];
        filterCoefs(filterGTab[v->filterCode], v->filterRes, c);          // control values read once per sub-block

        for (uint32_t i = sb; i < sb + RAMP_LEN; i++) {                   // section 1, with the input attenuator, in place
            const int32_t xq = smmulr(voiceTmp[i] >> (FILTER_PRE_SHIFT - FILTER_FRAC), attQ22) * 4;    // xq = input * att / 256
            voiceTmp[i] = filterSection(s0, s1, xq, c[0], c[1], c[2]);
        }
        for (uint32_t i = sb; i < sb + RAMP_LEN; i++) {                   // section 2 (resonant), voice gain, mix into the output (left = right)
            const int32_t y   = ssat24(filterSectionLim(s2, s3, voiceTmp[i], c[3], c[4], c[5]));       // never more than a full-scale voice
            const int32_t out = (y * (1 << (FILTER_PRE_SHIFT - FILTER_FRAC))) * gen;
            vb[0] += out;
            vb[1] += out;
            vb += 2;
        }
    }

// save the state for the next buffer
    v->filter.z[0] = s0;  v->filter.z[1] = s1;  v->filter.z[2] = s2;  v->filter.z[3] = s3;

    *waveAmplSin = sinAmpl;
    *waveAmplTri = triAmpl;
    *waveAmplSaw = sawAmpl;

    v->sqrTh      = sqrTh;
    v->noisePhase = nSeed;
    pink_state    = pink_state_loc;

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
