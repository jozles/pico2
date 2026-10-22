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
#include "filter_g_table.h"
#include "input_tables_management.h"
#include "sound_level_management.h"
#include "miscControls.h"
#include "hardware/sync.h"

extern int32_t       echoBus[MAX_ECHO][ECHO_STEPS];
extern volatile bool echoBusUsed[MAX_ECHO];

//extern bool tb7;
//static bool filterTraceDone = false;
volatile bool dumpRequest=false; 
volatile uint8_t dumpLine=0;

const uint8_t octNb = OCTNB;
float baseFreq = FREQ0;
float octFreq[octNb+1];

const uint16_t octIncrNb = 409;
float octIncr[octIncrNb];

#define FILTER_FRAC       8             // fractional bits kept inside the filter (Q8)
#define FILTER_PRE_SHIFT  15            // shift between the voice signal and the filter input


#define FILTER_RAMP_SHIFT 9                                    // log2(SAMPLES_PER_BUFFER): one coefficient ramp over the whole buffer
static_assert((1u << FILTER_RAMP_SHIFT) == SAMPLES_PER_BUFFER, "FILTER_RAMP_SHIFT must be log2(SAMPLES_PER_BUFFER)");
static int32_t voiceTmp[SAMPLES_PER_BUFFER];                   // mono signal of the voice being rendered, before the filter (voices run one after the other)

#define FILTER_RES_MAX 255                      // maximum resonance command: no damping in the second section


// -----

extern volatile uint32_t millisCounter;

extern uint16_t amplLevel[];
extern uint16_t waveLevel[WAVE_LEVEL_STEPS];    // wave amplitude of each step (0..0x7fff), defined in sound_level_management.cpp

volatile uint32_t i2sUnderruns=0;               // number of times both i2s buffers were free at once: the DMA ran out of data (audible glitch)

// current lfo values (lfosHandler is triggered by pwm_irq_handler)
float       lfosFrequency[MAX_LFO];             // frequency
uint16_t    lfosCodersFreq[MAX_LFO];            // frequency coder value
uint16_t    lfosMaxCoderFreq[MAX_LFO];          // frequency coder max value
uint16_t    lfosCodersFreqAtt[MAX_LFO];         // frequency input attenuator value

volatile uint32_t lfosPhaseStep[MAX_LFO];       // Q16.16
uint32_t          lfoPhase[MAX_LFO];            // Q16.16, replaces currLfoEch's fraction

uint16_t    currLfoEch[MAX_LFO];
//uint32_t    currLfoEchFra[MAX_LFO];
//int16_t     lfosOutputsValues[MAX_LFO][MAX_OUTPUTS_PER_OBJ];    // inutilisé ??
uint32_t    lfoTime=1;                                          // synchrone avec le handler adsr 
uint32_t    lfoTimingInterval=1000/LFOS_SAMPLE_RATE;  
//int32_t     lfoScopeBuffer[MAX_LFO*OSC_SCOPE_BUFFER_LEN];       // n° echantillons+rc_table 
int32_t     lfoScopeBufReal[MAX_LFO*OSC_SCOPE_BUFFER_LEN*BASIC_WAVES_NB];  // real values
uint16_t    lfoScopeBufPtr=0;

uint16_t    lfosCycleR[MAX_LFO];                      // cycle ratio 
uint16_t    lfosCoderCycleR[MAX_LFO];                 // cycle ratio coder value -64/+64

uint16_t    lfosCoderCycleRAtt[MAX_LFO];              // cycle ratio input attenuator value

int16_t     lfo_ctl_input_id[MAX_LFO][LFO_INPUTS_NB];    // id des inputs du lfo dans ctl_input_xxx[]
int16_t     lfo_ctl_output_id[MAX_LFO][MAX_OUTPUTS_PER_OBJ];  // id des outputs du lfo dans ctl_output_xxx[]
int16_t     lfo_first_output_id;
int16_t     lfo_first_input_id;

extern int16_t ctl_input_val[TOTAL_INPUTS_ID_REAL];
extern int16_t ctl_output_id_chain[MAX_OUTPUTS];
extern uint8_t ctl_input_update_type[TOTAL_INPUTS_ID_REAL];

int32_t     voicesScopeDataBuffer[MAX_VOICES*SAMPLES_PER_BUFFER];  // all voices data buffer 

// i2s

Voice voices[MAX_VOICES];
//int32_t w0[MAX_VOICES][BASIC_WAVES_NB];

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

const int32_t alpha = 32113;        // 0.98 en Q15

volatile int32_t pink_state = 0;    // Q15 interne

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
  fillAmplIncr();
}

// **********************  voices ************************

void voicesInit(Voice* voices,uint16_t coderF,uint8_t cga)    // cga = genAmpl coder
{
    printf("%u voices init\n",MAX_VOICES);
    for(uint8_t v=0;v<MAX_VOICES;v++){
        setVoiceCycleRCoders(&voices[v],MAXCODER_RC/2,FULL_ATTENUATION_VALUE);
        setVoiceFreqCoders(&voices[v],coderF,FULL_ATTENUATION_VALUE);
        setVoiceAmplCoders(&voices[v],BASIC_WAVES_NB,cga,FULL_ATTENUATION_VALUE);       // voice gain

        for(uint8_t w=0;w<BASIC_WAVES_NB;w++){
            setVoiceAmplCoders(&voices[v],w,0,FULL_ATTENUATION_VALUE);                  // every wave off
        }

        voices[v].phase=0;
        voices[v].noiseSeed=0;             // seed of the noise generator: 0 = initialised by the first buffer
        voices[v].sqrTh=1024;
        voices[v].coderReverb=0;
        voices[v].coderEcho=0;

        printf("voice %u: id=%d type=%d\n", v, voices[v].voice_ctl_input_id[VFIL], ctl_input_update_type[voices[v].voice_ctl_input_id[VFIL]]);
        filtersInit(v);
    }
}

void voicesInit(Voice* voices,float freq,uint8_t cga){
   voicesInit(voices,calcCoderFreq(freq),cga);
}

void dumpVoices(Voice* v, uint8_t line)                      // one voice on 3 lines + 6 wave lines; fillVoices() is called after each line: a long printf must not starve the i2s buffers
{
  extern volatile uint32_t dbgClickRatio, dbgClickBufs, i2sUnderruns;
  static const char*   waveNames[BASIC_WAVES_NB]={"sin","tri","saw","sqr","whi","pnk"};          // same order as WSIN..PONK
  if(line>=MAX_VOICES){return;}
  const Voice* p=&v[line];
  const float  f=p->frequency, cf=p->filterFrequency;                                              // frequencies are printed without %f (no float printf: it needs a lot of stack)
  const int    fi=(int)f,  ff=(int)((f-(float)fi)*1000.0f+0.5f);
  const int    ci=(int)cf, cfr=(int)((cf-(float)ci)*1000.0f+0.5f);

  printf("V%d VSPW=%d ids: [0]=%d [VSPW]=%d  f:%d/%d %d.%03dHz step:%lu.%lu rc:%d(c%d a%d) sqrTh:%ld gen:%d/%d=%d\n",line,
      (int)VSPW,p->voice_ctl_input_id[0],p->voice_ctl_input_id[VSPW],
      p->coderFreq,VCES_MAX_FREQ_CODERS,fi,ff,
      (unsigned long)(p->phaseStep>>PHASE_SHIFT),(unsigned long)(p->phaseStep&((1u<<PHASE_SHIFT)-1)),
      p->cycleR,p->coderCycleR,p->coderCycleRAtt,(long)p->sqrTh,p->coderGenAmpl,VCES_MAX_AMPL_CODERS,p->genAmpl);
  fillVoices();

  // amplitude path of every wave: coder/max, base level of the coder, attenuator, value of its amplitude input (slot VSPW + wave), current>target
  for(uint8_t wa=0;wa<BASIC_WAVES_NB;wa++){
      const uint16_t cc=(p->coderWaveAmpl[wa]<WAVE_LEVEL_STEPS)?p->coderWaveAmpl[wa]:(WAVE_LEVEL_STEPS-1);
      printf(" %s:%d/%d base%u att%d in%d %d>%d\n",waveNames[wa],p->coderWaveAmpl[wa],WAVE_LEVEL_STEPS,waveLevel[cc],p->coderWaveAmplAtt[wa],
          ctl_input_val[p->voice_ctl_input_id[VSPW+wa]],p->basicWaveAmpl[wa],p->newBasicWaveAmpl[wa]);
      fillVoices();
  }

  printf("   filter code:%d (c%d a%d) %d.%03dHz lev:%d res:%d (c%d a%d) z:%ld %ld %ld %ld\n",
      p->filterCode,p->coderFilterFreq,p->coderFilterFreqAtt,ci,cfr,p->coderFilterLevAtt,p->filterRes,p->coderFilterRes,p->coderFilterResAtt,
      (long)p->filter.z[0],(long)p->filter.z[1],(long)p->filter.z[2],(long)p->filter.z[3]);
  fillVoices();
  printf("click: ratio max %lu (x1e-4) over %lu buffers ; i2s underruns: %lu\n",(unsigned long)dbgClickRatio,(unsigned long)dbgClickBufs,(unsigned long)i2sUnderruns);
  dbgClickRatio=0; dbgClickBufs=0;                                                                 // peak hold: restarts after each dump
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
int32_t __not_in_flash_func(ctlCombine)(int32_t coder,int32_t att,int16_t input,uint8_t inShift,int32_t lo,int32_t hi)
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

// ---- setVoiceXxxCoders(Voice*, coders...): load the coders, then compute the internal values (same model as setVoiceFilter)
// ---- setVoiceXxx(Voice*)                 : compute only, from the stored coders and the stored value of the input (called by apply_input)

void __not_in_flash_func(setVoiceFrequency)(Voice* v)                                    // frequency -> phase step
{
    int16_t id = v->voice_ctl_input_id[VFRQ];
    float f = calcFreq((uint16_t)ctlCombine(v->coderFreq, v->coderFreqAtt, ctl_input_val[id], 6, 0, FREQ_CODE_MAX));
    setVoiceSteps(f, v);
}

void __not_in_flash_func(setVoiceFreqCoders)(Voice* v, uint16_t coderFreq, uint16_t coderFreqAtt)
{
    const uint32_t irq = save_and_disable_interrupts();                                   // the interrupt that updates the inputs computes the same values: no interleaving
    v->coderFreq    = coderFreq;
    v->coderFreqAtt = coderFreqAtt;
    setVoiceFrequency(v);
    restore_interrupts(irq);
}

void __not_in_flash_func(setVoiceCycleR)(Voice* v)                                       // cycle ratio -> table number
{
    int16_t id = v->voice_ctl_input_id[VCR_];
    v->cycleR = (uint8_t)ctlCombine(v->coderCycleR, v->coderCycleRAtt, ctl_input_val[id], 10, INT32_MIN, INT32_MAX);   // periodic table: fillVoiceBuffer_mono reads cycleR&63, and 256 is a multiple of 64
}

void __not_in_flash_func(setVoiceCycleRCoders)(Voice* v, uint8_t coderCycleR, int16_t coderCycleRAtt)
{
    const uint32_t irq = save_and_disable_interrupts();
    v->coderCycleR    = coderCycleR;
    v->coderCycleRAtt = coderCycleRAtt;
    setVoiceCycleR(v);
    restore_interrupts(irq);
}


// *************************** filters ****************************

#define FILTER_INPUT_SHIFT 6        // use the value the old VFIL case of update_inputs had

// ---- glide of the filter cutoff ----
// the cutoff code (409 codes per octave, i.e. a logarithmic frequency) goes through a one-pole low-pass, one step per ms:
// whatever the modulation does (sawtooth reset, steps of a coder), the render only sees a cutoff that moves smoothly
// 160 ms: first value for which the toc of a sawtooth on the cutoff is no longer heard (listening test)
#define FILTER_GLIDE_MS         160                     // time constant of the low-pass, ms (0 = no glide)
#define FILTER_GLIDE_START_MS   1500                    // no glide during the start-up: the presets must sound at once

static int32_t  filterCodeQ16[MAX_VOICES];              // smoothed cutoff code, Q16
static bool     filterGlideOn=false;

void __not_in_flash_func(filterGlideTick)()             // call it every ms (interrupt at 1 kHz)
{
    static uint16_t startTicks=0;
    if(!filterGlideOn){
        if(startTicks<FILTER_GLIDE_START_MS){startTicks++;return;}
        for(uint8_t v=0;v<MAX_VOICES;v++){                                                                 // start from the code the render is using: no jump, and a voice never set stays as it is
            voices[v].filterCodeTarget=voices[v].filterCode;
            filterCodeQ16[v]=((int32_t)voices[v].filterCode)<<16;
        }
        filterGlideOn=true;
    }
    const int32_t k=65536/(FILTER_GLIDE_MS+1);
    for(uint8_t v=0;v<MAX_VOICES;v++){
        int32_t s=filterCodeQ16[v];
        s+=(int32_t)(((int64_t)((((int32_t)voices[v].filterCodeTarget)<<16)-s)*k)>>16);
        filterCodeQ16[v]=s;
        voices[v].filterCode=(int16_t)((s+0x8000)>>16);                                                      // one 16-bit store, read once per sub-block by the render
    }
}

void __not_in_flash_func(setVoiceFilterFrequency)(Voice* v)
{
    int16_t id = v->voice_ctl_input_id[VFIL];
    int32_t code = ctlCombine(v->coderFilterFreq, v->coderFilterFreqAtt, ctl_input_val[id], FILTER_INPUT_SHIFT, 0, FILTER_CODE_MAX);
    v->filterFrequency = calcFilterFreq(code);                  // display: now includes the modulation
    v->filterCodeTarget = (int16_t)code;                        // cutoff asked by the coders and the modulation
    if(!filterGlideOn){                                         // start-up: the render sees the code at once
        v->filterCode = (int16_t)code;                          // one 16-bit store, read once per sub-block
        filterCodeQ16[v-voices] = code<<16;                     // the glide will start from here
    }
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

    setVoiceFilterFrequency(v);                                 // cutoff code = coder + attenuated stored input value
    setVoiceFilterRes(v);                                       // resonance   = coder + attenuated stored input value
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

// compute only: the frequency from the stored coders and the stored value of the input
void __not_in_flash_func(setLfosFrequency)(uint8_t lfo)
{
    int16_t id = lfo_ctl_input_id[lfo][LFRQ];
    float f=calcFreq((uint16_t)ctlCombine(lfosCodersFreq[lfo],lfosCodersFreqAtt[lfo],ctl_input_val[id],3,0,FREQ_CODE_MAX))/VOICE_FREQ_DIVIDER;
    setLfosSteps(f,lfo);
}

// load the coders of the frequency, then compute it
void __not_in_flash_func(setLfosFrCoders)(uint8_t lfo, uint16_t coderFreq, uint16_t coderAtt)
{
    const uint32_t irq=save_and_disable_interrupts();                       // the interrupt that updates the inputs computes the same value: no interleaving
    lfosCodersFreq[lfo]    = coderFreq;
    lfosCodersFreqAtt[lfo] = coderAtt;
    setLfosFrequency(lfo);
    restore_interrupts(irq);
}

// compute only: the cycle ratio from the stored coders and the stored value of the input
void __not_in_flash_func(setLfosCycleR)(uint8_t lfo)
{
    const int16_t valeur = ctl_input_val[lfo_ctl_input_id[lfo][LCR_]];
    lfosCycleR[lfo]=(uint16_t)(lfosCoderCycleR[lfo]+(((valeur>>10)*lfosCoderCycleRAtt[lfo])>>MAX_CTL_ATT_SHIFT));   // base + attenuated input ; read as &63 by lfosHandler
}

// load the coders of the cycle ratio, then compute it
void __not_in_flash_func(setLfosCycleRCoders)(uint8_t lfo, uint16_t coderCycleR, uint16_t coderCycleRAtt)
{
    const uint32_t irq=save_and_disable_interrupts();
    lfosCoderCycleR[lfo]    = coderCycleR;
    lfosCoderCycleRAtt[lfo] = coderCycleRAtt;
    setLfosCycleR(lfo);
    restore_interrupts(irq);
}

// same as setLfosCycleRCoders: kept because tests.cpp still calls this name ; to remove when tests.cpp is rewritten
void __not_in_flash_func(setLfosCycleR)(uint8_t lfo, uint16_t coderCycleR, uint16_t coderCycleRAtt)
{
    setLfosCycleRCoders(lfo,coderCycleR,coderCycleRAtt);
}

void lfosInit(){
    printf("%u lfos init\n",MAX_LFO);
    for(uint8_t l=0;l<MAX_LFO;l++){

        lfosMaxCoderFreq[l]=LFOS_MAX_FREQ_CODERS;
        currLfoEch[l]=0;
        lfosPhaseStep[l]=0; 
        lfoPhase[l]=0;
        
        //for(uint8_t v=0;v<MAX_OUTPUTS_PER_OBJ;v++){lfosOutputsValues[l][v]=0;}

        setLfosFrCoders(l, 1768, FULL_ATTENUATION_VALUE);               // 1.5 s
        setLfosCycleRCoders(l, MAXCODER_RC/2, FULL_ATTENUATION_VALUE);

        lfoTime=0;
    }
    //memset(lfoScopeBuffer,0x0000,sizeof(lfoScopeBuffer));

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
//gpio_put(TST_PIN,1);    
    lfoTime=millisCounter;

    const int16_t *base32 = &rc_tables[32][0][0];                           // base table 32 pour saw
    
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
        
        const int16_t *w = &rc_tables[rcTableNb][0][0]+RC_N_WAVES*ce_idx;    // pointer into the rc table 
        
        int16_t c0=sign*w[LSIN];
        int16_t out_id;
        out_id=ctl_output_id_chain[lfo_ctl_output_id[l][LSIN]];
        if (__builtin_expect(out_id != NO_LINK, 0)){update_inputs(out_id,c0);}

        int16_t c1=sign*w[LTRI];
        out_id=ctl_output_id_chain[lfo_ctl_output_id[l][LTRI]];
        if (__builtin_expect(out_id != NO_LINK, 0)){update_inputs(out_id,c1);}

        int16_t tri=base32[RC_N_WAVES*ce+LTRI];
        int32_t c2;
        if(ce<(RC_N_SAMPLES >> 1)){c2=(65536-tri)>>1;}
        else c2=tri>>1;
        c2=sign*c2;
        if(rc>=32){c2=-c2;}
        out_id=ctl_output_id_chain[lfo_ctl_output_id[l][LSAW]];
        if (__builtin_expect(out_id != NO_LINK, 0)){update_inputs(out_id,c2);}

        int16_t c3=(currLfoEch[l] & (BASIC_WAVE_TABLE_LEN>>1)) ? -0x7fff : 0x7fff;
        out_id=ctl_output_id_chain[lfo_ctl_output_id[l][LSQR]];
        if (__builtin_expect(out_id != NO_LINK, 0)){update_inputs(out_id,c3);}
        
        uint32_t bufScopeOffset=l*OSC_SCOPE_BUFFER_LEN*BASIC_WAVES_NB + lfoScopeBufPtr*BASIC_WAVES_NB;
        lfoScopeBufReal[bufScopeOffset+LSIN]=c0;
        lfoScopeBufReal[bufScopeOffset+LTRI]=c1;
        lfoScopeBufReal[bufScopeOffset+LSAW]=c2;
        lfoScopeBufReal[bufScopeOffset+LSQR]=c3;
      }

    lfoScopeBufPtr++;
    lfoScopeBufPtr&=OSC_SCOPE_BUFFER_LEN-1;
//gpio_put(TST_PIN,0);    
  }
}

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
    int32_t        sinAcc, sinInc;      // amplitude ramps: current value Q16, and step per sample
    int32_t        triAcc, triInc;
    int32_t        sawAcc, sawInc;
    uint32_t       sqrTh;               // width of the square pulse, in table units (0..2048)
    int32_t        sqrAHi, sqrALo;      // square levels, already multiplied by the square amplitude
    int32_t        whiAmpl, pnkAmpl;
    uint32_t       seed;                // noise generator state (xorshift32)
    int32_t        pink;
};

// ---- sawtooth without a table (experimental) ----
// the sawtooth is the triangle of rc table 0. According to the measurement made at boot (first 0, last 32) that table is a ramp that starts at the peak
// (position 1) and falls to about 0 at position 1023, with 0 at position 0: seen from j = (-position) & 1023 it is a straight line
// through the origin: value = sawLineB + ((j * sawLineK) >> 16)
// sawLineInit() fits that line on the table once, prints what it finds and, when SAW_FROM_TABLE is 0, stops the program if the table is not that line
#define SAW_FROM_TABLE  1                               // 1: the sawtooth is read in rc table 0 (always right) ; 0: it is computed from the phase (after the boot message says "gap 0" or "gap 1")

static int32_t sawLineB = 0;                            // offset of the line, chosen so that the gap to the table is the smallest
static int32_t sawLineK = 0;                            // slope per step of j, Q16
static bool    sawLineReady = false;

static void sawLineInit()
{
    const int32_t y0 = rc_tables[0][0][WTRI];                           // j = 0
    const int32_t y1 = rc_tables[0][1][WTRI];                           // j = 1023
    int32_t k = 0;
    if (y1 - y0 <= 32767 && y0 - y1 <= 32767) { k = (int32_t)(((int64_t)(y1 - y0) * 65536) / (RC_TABLES_LEN - 1)); }   // (j * K) must fit in 32 bits

    int32_t lo = INT32_MAX, hi = INT32_MIN;
    for (int32_t ce = 0; ce < RC_TABLES_LEN; ce++) {
        const int32_t j = (-ce) & (RC_TABLES_LEN - 1);
        const int32_t d = (int32_t)rc_tables[0][ce][WTRI] - (int32_t)(((int64_t)j * k) >> 16);
        if (d < lo) lo = d;
        if (d > hi) hi = d;
    }
    const int32_t b = (lo + hi) >> 1;                                   // centres the gap: at most about +-1 on a rounded straight line

    int32_t maxDev = 0;
    for (int32_t ce = 0; ce < RC_TABLES_LEN; ce++) {
        const int32_t j = (-ce) & (RC_TABLES_LEN - 1);
        int32_t d = (int32_t)rc_tables[0][ce][WTRI] - (b + (int32_t)(((int64_t)j * k) >> 16));
        if (d < 0) d = -d;
        if (d > maxDev) maxDev = d;
    }
    printf("saw table 0: [0]=%ld [1]=%ld [2]=%ld [3]=%ld [512]=%ld [1022]=%ld [1023]=%ld ; line offset %ld slope %ld ; gap %ld\n",
           (long)rc_tables[0][0][WTRI], (long)rc_tables[0][1][WTRI], (long)rc_tables[0][2][WTRI], (long)rc_tables[0][3][WTRI],
           (long)rc_tables[0][512][WTRI], (long)rc_tables[0][1022][WTRI], (long)rc_tables[0][1023][WTRI], (long)b, (long)k, (long)maxDev);
#if !SAW_FROM_TABLE
    if (maxDev > 2) { system_error("saw tbl0 not a line", maxDev); }    // short message: it is drawn on the display
#endif
    sawLineB = b;
    sawLineK = k;
    sawLineReady = true;
}

// one sub-block of the oscillator, specialised at compile time for the waves that are active:
// no per-sample test, and the variables of the muted waves do not exist in the loop
static_assert(BASIC_WAVE_TABLE_LEN == 2 * RC_TABLES_LEN, "the mirror below assumes a table of half a period");

template<bool kTri, bool kSaw, bool kSqr, bool kWhite, bool kPink>
static inline __attribute__((always_inline)) void oscBlock(OscBlock& b)
{
    const int32_t* idx    = b.idx;
    int32_t*       out    = b.out;
    const int16_t* rcCurr = b.rcCurr;
    const uint32_t revMask = b.reverse ? (RC_TABLES_LEN - 1) : 0u;     // x ^ 1023 = 1023 - x for x in 0..1023: the reversed table read without a test
    int32_t sinAcc = b.sinAcc;  const int32_t sinInc = b.sinInc;
    int32_t triAcc = b.triAcc;  const int32_t triInc = b.triInc;
    int32_t sawAcc = b.sawAcc;  const int32_t sawInc = b.sawInc;
#if !SAW_FROM_TABLE
    const int32_t sawB = sawLineB, sawK = sawLineK;                    // the sawtooth line, copied into registers (the output pointer could alias a global)
#endif
    const uint32_t sqrTh = b.sqrTh;  const int32_t sqrAHi = b.sqrAHi, sqrALo = b.sqrALo;
    const int32_t whiAmpl = b.whiAmpl, pnkAmpl = b.pnkAmpl;
    uint32_t seed = b.seed;
    int32_t  pink = b.pink;

    for (uint32_t s = 0; s < RAMP_LEN; s++) {
        const uint32_t t = idx[s];                                     // raw phase index 0..2047

        // first half period (t < 1024): the table is read forwards ; second half: backwards, and the sign is inverted
        const uint32_t mask = 0u - (t >> 10);                          // 0 in the first half, 0xFFFFFFFF in the second
        const uint32_t ce   = (t ^ mask) & (RC_TABLES_LEN - 1);        // mirrored position in the table, 0..1023
        const int32_t  sign = (int32_t)(mask | 1u);                    // +1 or -1

        const int16_t* w = rcCurr + RC_N_WAVES * (ce ^ revMask);       // pointer into the rc table

        sinAcc += sinInc;
        int32_t pre = smmulr(w[WSIN] * 65536, sinAcc);                 // wave * amplitude; the ramped amplitude keeps 16 fractional bits

        if (kTri) {
            triAcc += triInc;
            pre += smmulr(w[WTRI] * 65536, triAcc);
        }
        if (kSaw) {
            sawAcc += sawInc;
#if SAW_FROM_TABLE
            pre += smmulr(rc_tables[0][ce][WTRI] * 65536, sawAcc);     // sawtooth = triangle of table 0, the same for every rc
#else
            const int32_t sawj = (int32_t)((0u - ce) & (RC_TABLES_LEN - 1));      // table 0 seen from its end: a straight line through the origin
            const int32_t sawv = sawB + ((sawj * sawK) >> 16);                     // no table read
            pre += smmulr(sawv * 65536, sawAcc);
#endif
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

#define SUBBLOCKS       (SAMPLES_PER_BUFFER / RAMP_LEN)      // sub-blocks per buffer (4)
#define SUBBLOCKS_SHIFT 2                                       // log2(SUBBLOCKS)
static_assert((1 << SUBBLOCKS_SHIFT) == SUBBLOCKS, "SUBBLOCKS_SHIFT must be log2(SUBBLOCKS)");
#define GEN_RAMP_SHIFT  9                                       // the voice gain ramps over the whole buffer: 2^9 = 512 samples
static_assert((1u << GEN_RAMP_SHIFT) == SAMPLES_PER_BUFFER, "GEN_RAMP_SHIFT must be log2(SAMPLES_PER_BUFFER)");

static int32_t genLast[MAX_VOICES];                             // voice gain reached at the end of the previous buffer
static int32_t levLast[MAX_VOICES];                             // filter input attenuator reached at the end of the previous buffer
static int32_t echoAttLast[MAX_VOICES];                         // echo send level reached at the end of the previous buffer
static bool    voiceStarted[MAX_VOICES];                        // false until the first buffer of the voice

void __not_in_flash_func(fillVoiceBuffer_mono)(volatile int32_t* vBuffer,Voice* v,uint8_t voiceNum)
{
    if (__builtin_expect(!rcPeakReady, 0)) { fillRcPeak(); }              // first buffer only, before the interrupts are masked
    if (__builtin_expect(!sawLineReady, 0)) { sawLineInit(); }            // first buffer only, before the interrupts are masked

    uint32_t ints = save_and_disable_interrupts();
gpio_put(TST_PIN,1);

    i2s_buf_scope=vBuffer;

    const uint32_t phaseStep = v->phaseStep;                              // read once for the whole buffer

// init noise: one xorshift32 generator per voice (its state must never be 0); v->noiseSeed holds the state
    uint32_t nSeed = v->noiseSeed;
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

// wave amplitudes: the current values are stored in basicWaveAmpl[], the targets are written in newBasicWaveAmpl[] by setVoiceAmpl()
    volatile int16_t* waveAmplSin    = &v->basicWaveAmpl[WSIN];
    volatile int16_t* newWaveAmplSin = &v->newBasicWaveAmpl[WSIN];
    volatile int16_t* waveAmplTri    = &v->basicWaveAmpl[WTRI];
    volatile int16_t* newWaveAmplTri = &v->newBasicWaveAmpl[WTRI];
    volatile int16_t* waveAmplSaw    = &v->basicWaveAmpl[WSAW];
    volatile int16_t* newWaveAmplSaw = &v->newBasicWaveAmpl[WSAW];
    volatile int16_t* waveAmplSqr    = &v->basicWaveAmpl[WSQR];
    volatile int16_t* newWaveAmplSqr = &v->newBasicWaveAmpl[WSQR];
    volatile int16_t* waveAmplWhi    = &v->basicWaveAmpl[WHIT];
    volatile int16_t* newWaveAmplWhi = &v->newBasicWaveAmpl[WHIT];
    volatile int16_t* waveAmplPnk    = &v->basicWaveAmpl[PONK];
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
// every amplitude moves from its value at the start of the buffer to its target during the buffer (11.6 ms, no sudden step):
// sin, tri and saw by a per-sample ramp inside each sub-block, square and noises by 4 small steps
    const bool    first    = !voiceStarted[voiceNum];                                                  // first buffer of this voice: gain and filter level start at their targets, waves rise during the first sub-block
    const int32_t sinEnd   = *newWaveAmplSin, triEnd   = *newWaveAmplTri, sawEnd   = *newWaveAmplSaw;      // targets, read once per buffer
    const int32_t sqrEnd   = *newWaveAmplSqr, whiEnd   = *newWaveAmplWhi, pnkEnd   = *newWaveAmplPnk;
    const int32_t sinStart = first ? 0 : *waveAmplSin,  triStart = first ? 0 : *waveAmplTri,  sawStart = first ? 0 : *waveAmplSaw;   // first buffer: waves rise from 0 during the first sub-block
    const int32_t sqrStart = first ? sqrEnd : *waveAmplSqr,  whiStart = first ? whiEnd : *waveAmplWhi,  pnkStart = first ? pnkEnd : *waveAmplPnk;
    int32_t sinAmpl = sinStart << 16, triAmpl = triStart << 16, sawAmpl = sawStart << 16;                  // running amplitudes of the ramped waves, Q16

    for (uint32_t sb = 0; sb < SAMPLES_PER_BUFFER; sb += RAMP_LEN)        // one pass per sub-block
    {
        const int32_t jb = (int32_t)(sb >> RAMP_SHIFT) + 1;               // sub-block number, 1..SUBBLOCKS
        // the waves ramp in Q16 (16 fractional bits): even amplitudes of a few units ramp smoothly, evenly over the 4 sub-blocks
        const int32_t sinNext = first ? (sinEnd << 16) : (sinStart << 16) + (((sinEnd - sinStart) * jb) << (16 - SUBBLOCKS_SHIFT));    // value reached at the end of this sub-block
        const int32_t triNext = first ? (triEnd << 16) : (triStart << 16) + (((triEnd - triStart) * jb) << (16 - SUBBLOCKS_SHIFT));    // (first buffer: the target already at the end of sub-block 1)
        const int32_t sawNext = first ? (sawEnd << 16) : (sawStart << 16) + (((sawEnd - sawStart) * jb) << (16 - SUBBLOCKS_SHIFT));
        const int32_t sqrNow  = sqrStart + ((sqrEnd - sqrStart) * jb) / (int32_t)SUBBLOCKS;     // value used during this sub-block
        const int32_t whiNow  = whiStart + ((whiEnd - whiStart) * jb) / (int32_t)SUBBLOCKS;
        const int32_t pnkNow  = pnkStart + ((pnkEnd - pnkStart) * jb) / (int32_t)SUBBLOCKS;

        // per-sample ramps
        const int32_t sinInc = (sinNext - sinAmpl) >> RAMP_SHIFT;         // step per sample, Q16
        const int32_t sinAcc = sinAmpl;                                   // running amplitude, Q16
        const int32_t triInc = (triNext - triAmpl) >> RAMP_SHIFT;
        const int32_t triAcc = triAmpl;
        const int32_t sawInc = (sawNext - sawAmpl) >> RAMP_SHIFT;
        const int32_t sawAcc = sawAmpl;

        // muted-wave flags, evaluated once per sub-block
        const bool whiteOn = (whiNow != 0);
        const bool pinkOn  = (pnkNow != 0);
        const bool wtriOn  = ((triAmpl | triNext) != 0);                  // running amplitude or end value: the ramp must be able to start and to finish
        const bool wsawOn  = ((sawAmpl | sawNext) != 0);
        const bool wsqrOn  = (sqrNow != 0);

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
            b.sqrTh  = (uint32_t)sqrTh;
            b.sqrAHi =  (int32_t)((1.0f - sqrD) * sqrK) * sqrNow;         // zero-mean levels, multiplied by the amplitude once per sub-block
            b.sqrALo = -(int32_t)(sqrD * sqrK) * sqrNow;
        }
        b.whiAmpl = whiNow;  b.pnkAmpl = pnkNow;
        b.seed = nSeed;  b.pink = pink_state_loc;

        OSC_DISPATCH((wtriOn ? 1 : 0) | (wsawOn ? 2 : 0) | (wsqrOn ? 4 : 0) | (whiteOn ? 8 : 0) | (pinkOn ? 16 : 0))

        nSeed = b.seed;  pink_state_loc = b.pink;

        sinAmpl = sinNext;                                                // exact landing at the end of the sub-block
        triAmpl = triNext;
        sawAmpl = sawNext;
    }

// filter: two state-variable sections in cascade (Butterworth 4th order), coefficients refreshed every sub-block
    int32_t s0 = v->filter.z[0], s1 = v->filter.z[1], s2 = v->filter.z[2], s3 = v->filter.z[3];    // integrator states of section 1 and section 2
    int32_t levEnd = v->coderFilterLevAtt;                                // input attenuator: ramps from its value at the end of the previous buffer
    if (levEnd > MAX_CTL_ATT) levEnd = MAX_CTL_ATT;
    if (levEnd < 0)           levEnd = 0;
    const int32_t levFrom  = first ? levEnd : levLast[voiceNum];
    const int32_t attInc = (levEnd - levFrom) << (22 - GEN_RAMP_SHIFT);   // per-sample step, Q22
    int32_t       attAcc = levFrom << 22;                                 // 256 << 22 = 2^30: unity gain fits in a signed 32-bit word

    const int32_t genEnd   = *waveAmplGen;                                // voice gain: ramps from its value at the end of the previous buffer
    const int32_t genFrom  = first ? genEnd : genLast[voiceNum];
    const int32_t genInc   = genEnd - genFrom;
    int32_t       genAcc   = genFrom << GEN_RAMP_SHIFT;

    // output limiter: y * 128 * gen must stay below 2^31, otherwise the product wraps around and gives loud noise ; y is clipped instead
    const int32_t genMax = (genEnd > genFrom) ? genEnd : genFrom;
    const int32_t yMax   = (genMax > 2) ? (0x7fffffff / (128 * genMax)) : 8388607;      // never more than 2^23 - 1 (a full-scale voice)

    // echo send: level read once per buffer, 4 steps per buffer from its value at the end of the previous buffer
    const int32_t echoEnd  = v->echoAtt;
    const int32_t echoFrom = first ? echoEnd : echoAttLast[voiceNum];
    const bool    echoOn   = ((echoEnd | echoFrom) != 0);                 // a voice whose send stays at 0 costs nothing
    int32_t*      eb       = echoBus[0];                                  // the voices send to echo 0
    if (echoOn) { echoBusUsed[0] = true; }

    volatile int32_t* vb = vBuffer;
    for (uint32_t sb = 0; sb < SAMPLES_PER_BUFFER; sb += RAMP_LEN) {
        int32_t c[6];
        filterCoefs(filterGTab[v->filterCode], v->filterRes, c);          // control values read once per sub-block
        const int32_t jb    = (int32_t)(sb >> RAMP_SHIFT) + 1;            // sub-block number, 1..SUBBLOCKS
        const int32_t echoQ = (echoFrom + ((echoEnd - echoFrom) * jb) / (int32_t)SUBBLOCKS) << 16;    // send level of this sub-block, scaled for smmulr

        for (uint32_t i = sb; i < sb + RAMP_LEN; i++) {                   // section 1, with the input attenuator, in place
            attAcc += attInc;
            const int32_t xq = smmulr(voiceTmp[i] >> (FILTER_PRE_SHIFT - FILTER_FRAC), attAcc) * 4;    // xq = input * att / 256
            voiceTmp[i] = filterSection(s0, s1, xq, c[0], c[1], c[2]);
        }
        for (uint32_t i = sb; i < sb + RAMP_LEN; i += 2) {                // section 2 (resonant), voice gain, mix into the output (left = right), echo send: two samples per pass
            int32_t o[2];
            for (uint32_t k = 0; k < 2; k++) {
                int32_t y = filterSectionLim(s2, s3, voiceTmp[i + k], c[3], c[4], c[5]);
                if (y >  yMax) y =  yMax;                                 // clip: never more than a full-scale voice, and no overflow in the gain product
                if (y < -yMax) y = -yMax;
                genAcc += genInc;
                o[k] = (y * (1 << (FILTER_PRE_SHIFT - FILTER_FRAC))) * (genAcc >> GEN_RAMP_SHIFT);
                vb[0] += o[k];
                vb[1] += o[k];
                vb += 2;
            }
            if (echoOn) { eb[i >> 1] += smmulr((o[0] >> 1) + (o[1] >> 1), echoQ); }   // mean of the pair (22050 Hz) times the send level, halved: the bus keeps 6 dB more headroom than the mix
        }
    }

// save the state for the next buffer
    v->filter.z[0] = s0;  v->filter.z[1] = s1;  v->filter.z[2] = s2;  v->filter.z[3] = s3;
    genLast[voiceNum] = genEnd;
    levLast[voiceNum] = levEnd;
    echoAttLast[voiceNum] = echoEnd;
    voiceStarted[voiceNum] = true;

    *waveAmplSin = sinEnd;                                                // the ramps have reached their targets
    *waveAmplTri = triEnd;
    *waveAmplSaw = sawEnd;
    *waveAmplSqr = sqrEnd;
    *waveAmplWhi = whiEnd;
    *waveAmplPnk = pnkEnd;

    v->sqrTh      = sqrTh;
    v->noiseSeed  = nSeed;
    pink_state    = pink_state_loc;

gpio_put(TST_PIN,0);
    restore_interrupts(ints);
}


// ---- click detector (debug): looks for sudden steps in the mixed output ----
// the second difference x[n] - 2*x[n-1] + x[n-2] of a smooth tone stays close to w^2 * peak (w = 2*pi*f/fs); a step in the signal makes it much larger
volatile uint32_t dbgClickRatio = 0;        // largest (|second difference| / peak) x 10000 over the buffers examined since the last dump
volatile uint32_t dbgClickBufs  = 0;        // number of buffers examined since the last dump
static int32_t    dbgPrev1 = 0, dbgPrev2 = 0;

/*static void clickCheck(const int32_t* buf)  // left channel of an interleaved stereo buffer
{
    int32_t  p1 = dbgPrev1, p2 = dbgPrev2;
    uint32_t maxD2 = 0, peak = 1;
    for (uint32_t i = 0; i < SAMPLES_PER_BUFFER; i++) {
        const int32_t x  = buf[2 * i];
        int64_t d2 = (int64_t)x - 2 * (int64_t)p1 + (int64_t)p2;
        if (d2 < 0) d2 = -d2;
        if (d2 > (int64_t)maxD2) maxD2 = (uint32_t)((d2 > 0xffffffffLL) ? 0xffffffffu : d2);
        const uint32_t ax = (uint32_t)((x < 0) ? -(int64_t)x : x);
        if (ax > peak) peak = ax;
        p2 = p1;  p1 = x;
    }
    dbgPrev1 = p1;  dbgPrev2 = p2;
    const uint32_t ratio = (uint32_t)(((uint64_t)maxD2 * 10000u) / peak);
    if (ratio > dbgClickRatio) dbgClickRatio = ratio;
    dbgClickBufs++;
}*/

// ---- envelope log (debug): while an envelope runs, the peak of every buffer is stored, to see the amplitude as it is really rendered ----
// mute the other voices (testSetup: action=VOICE1): the log follows the mixed output ; envLogDump() prints it when the envelope is over
// with the peak: the filter cutoff code and the frequency of the voice (to see whether a jump of the level comes from a jump of one of them)
#define ENV_LOG_ADSR  1                                 // envelope to follow
#define ENV_LOG_VOICE 1                                 // voice whose cutoff code and frequency are logged
#define ENV_LOG_LEN   700                               // buffers (700 x 11.6 ms = 8 s)

extern uint8_t adsrStatus[MAX_ADSR];                    // miscControls.cpp ; 0 = ADSR_OFF, 1 = att, 2 = dec, 3 = sus, 4 = rel

static uint32_t envLogPeak[ENV_LOG_LEN];
static uint32_t envLogFreq[ENV_LOG_LEN];
static uint16_t envLogFilt[ENV_LOG_LEN];
static uint8_t  envLogState[ENV_LOG_LEN];
static uint16_t envLogN=0;
static bool     envLogRun=false;
static volatile bool envLogDone=false;

static void envLogPush(const int32_t* buf,const Voice* vv)  // called after every buffer: left channel of the mixed buffer
{
    const uint8_t st=adsrStatus[ENV_LOG_ADSR];
    if(!envLogRun){
        if(st==0 || envLogDone){return;}                // wait for the envelope to start, and for the previous log to be printed
        envLogRun=true;
        envLogN=0;
    }
    uint32_t peak=0;
    for(uint32_t i=0;i<SAMPLES_PER_BUFFER;i++){
        const int32_t x=buf[2*i];
        const uint32_t ax=(uint32_t)((x<0)?-(int64_t)x:x);
        if(ax>peak){peak=ax;}
    }
    if(envLogN<ENV_LOG_LEN){
        envLogPeak[envLogN]=peak>>8;                                                    // peak on 24 bits, like the dac
        envLogFilt[envLogN]=(uint16_t)vv[ENV_LOG_VOICE].filterCode;
        envLogFreq[envLogN]=(uint32_t)(vv[ENV_LOG_VOICE].frequency*100.0f);             // Hz x 100
        envLogState[envLogN]=st;
        envLogN++;
    }
    if(st==0 || envLogN>=ENV_LOG_LEN){envLogRun=false;envLogDone=true;}
}

void envLogDump()                                       // call it from the main loop: prints the log when an envelope has just ended
{
    if(!envLogDone){return;}
    printf("env log: ADSR %d, voice %d, %u buffers of 11.6 ms ; columns: n state(1=att 2=dec 3=sus 4=rel 0=off) peak(24 bits) filterCode freq(Hz x100)\n",ENV_LOG_ADSR,ENV_LOG_VOICE,envLogN);
    for(uint16_t i=0;i<envLogN;i++){
        printf("%u %u %lu %u %lu\n",i,envLogState[i],(unsigned long)envLogPeak[i],envLogFilt[i],(unsigned long)envLogFreq[i]);
        if((i&7)==7){fillVoices();}                     // keeps the audio running during the print
    }
    envLogDone=false;                                   // the next envelope can be logged
}

void __not_in_flash_func(fillVoiceBuffer)(int32_t* vBuffer, Voice* voices, uint8_t bufNum)
{
    blank_(vBuffer,SAMPLES_PER_BUFFER*2*4,0x00);   // env 12uS

    for(uint8_t v=0;v<MAX_VOICES;v++){
      fillVoiceBuffer_mono(vBuffer, &voices[v],v);
    }
    echoProcess(vBuffer);                          // echo of the voices that send to it, added to the mix
    //clickCheck(vBuffer);                           // debug: remove when done
    envLogPush(vBuffer,voices);                    // debug: remove when done
    i2s_buf_free[bufNum] = false;
}



void fillVoices()
{
    if(i2s_buf_free[0] && i2s_buf_free[1]){i2sUnderruns++;}       // both buffers consumed before one was refilled
    if(i2s_buf_free[0]){
      fillVoiceBuffer(i2s_buffer[0],voices,0);
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
