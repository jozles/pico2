#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/timer.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include <math.h>
#include "hardware/sync.h"

#include "bb_i2s.h"
#include "const.h"
#include "tests.h"
#include "std_utils.h"
#include "coder.h"
#include "frequences.h"
#include "leds.h"
#include "st7789.h"
#include "menus.h"
#include "input_tables_management.h"
#include "sound_level_management.h"
#include "miscControls.h"

extern struct Voice voices[];

extern int16_t objects_first_input_id[OBJECT_TYPES_NB];
extern int16_t objects_first_output_id[OBJECT_TYPES_NB];

extern int16_t  ctl_input_srce[TOTAL_INPUTS_ID_REAL];
extern char     ctl_input_name[TOTAL_INPUTS_ID_REAL][IN_OUT_NAME_LEN]; 
extern char     ctl_output_name[MAX_OUTPUTS][IN_OUT_NAME_LEN];

extern float    lfosFrequency[MAX_LFO];                   
extern uint16_t lfosCodersFreq[MAX_LFO];

extern uint16_t adsrCoderLev[MAX_ADSR];

extern uint16_t lfmCoder[LFM_INPUTS_NB][MAX_LFM];
extern uint16_t lfmCoderAtt[LFM_INPUTS_NB][MAX_LFM];

extern int16_t  lfm_ctl_input_id[LFM_INPUTS_NB][MAX_LFM];
extern int16_t  lfm_ctl_output_id[MAX_OUTPUTS_PER_OBJ][MAX_LFM];

extern const char objects_names[][OBJECTS_NAME_LEN];

extern volatile bool* coderTouchB;
extern bool oldCTB[CODER_NB];

// **** tb ****

bool tbget(uint8_t tb)  // toogle sur les rising edges de coderTouchB
{
    if(!oldCTB[tb] && (coderTouchB[tb]==1)){oldCTB[tb]=true;}
    if(oldCTB[tb] && (coderTouchB[tb]==0)){oldCTB[tb]=false;}
    return oldCTB[tb];
}

// ================= test Setup =====================

void obj_connect(int16_t inp_id,int16_t out_id)
{
    if(inp_id<=0 || inp_id>=TOTAL_INPUTS_ID_REAL || out_id<0 || out_id>=MAX_OUTPUTS){
        printf("obj_connect out of range: inp=%i out=%i\n",inp_id,out_id);
        return;
    }

    // make sure input is properly disconnected
    disconnect_input(inp_id,ctl_input_srce[inp_id]);

    // connect input on output via id chain
    connect_input(inp_id,out_id);

    char libi[IN_OUT_NAME_LEN+1];memset(libi,0x00,IN_OUT_NAME_LEN+1);
    memcpy(libi,&ctl_input_name[inp_id][0],IN_OUT_NAME_LEN);
    char libo[IN_OUT_NAME_LEN+1];memset(libo,0x00,IN_OUT_NAME_LEN+1);
    memcpy(libo,&ctl_output_name[ctl_input_srce[inp_id]],IN_OUT_NAME_LEN);    
    printf("%u:%s - %u:%s\n",inp_id,libi,out_id,libo); 
}

void sub_lfo(uint8_t object_type,uint8_t object_nb,uint8_t obj_inp_nb,uint8_t vInpNb,int8_t lfo,uint16_t coder,uint8_t lOutNb)
{
    uint8_t launcher;
    if(lfo>=0){
        launcher=LFO______;
        setLfosFrCoders(lfo,coder,0);
        setLfosFrequency(lfo);
        setLfosCycleR(lfo,MAXCODER_RC/2,0);
    }
    else {
        launcher=TBUTTON__;
        lfo=-lfo-1;    // numéro du touchb
        lOutNb=0;
    }
    
    // CX (voice)vInpNb (lfo out touche)lOutNb
    printf("(%i-%i-%i) %s:%u inp:%u lfo:%u outNb:%u f:%f",
        objects_first_input_id[VOICE____],objects_first_output_id[launcher],objects_first_output_id[ADSR_____],objects_names[object_type],object_nb,vInpNb,lfo,lOutNb,lfosFrequency[lfo]);
    obj_connect(objects_first_input_id[object_type]+object_nb*obj_inp_nb+vInpNb,objects_first_output_id[launcher]+lfo*MAX_OUTPUTS_PER_OBJ+lOutNb);
}

void voiceConfig(uint8_t voice,uint8_t wave,uint16_t coderFreq,uint8_t attenFreqLevel,uint8_t freqLfo,uint16_t freqLfoCoder,
    uint8_t manualAmpLevel,uint8_t attenAmpLevel,uint8_t adsr,int8_t adsrLfo,uint16_t adsrLfoFreqCoder,int8_t cr,uint8_t crAtt)       // adsrLfo >=0 lfo ; <0 touch
{
    setVoiceCycleRCoders(&voices[voice],cr+RC_TABLES_NB-1,crAtt);
    setVoiceAmplCoders(&voices[voice],wave,manualAmpLevel,attenAmpLevel);        // manual level, input level attenuator
    setVoiceFreqCoders(&voices[voice],coderFreq,attenFreqLevel);

    if(freqLfo!=0){sub_lfo(VOICE____,voice,VOICES_INPUTS_NB,VFRQ,freqLfo,freqLfoCoder,WTRI);}    // modulation freq de la voix

    // CX (voice)SIP (adsr)
    printf("Amp ctl adsr:%u ",adsr);
    obj_connect(objects_first_input_id[VOICE____]+voice*VOICES_INPUTS_NB+VSPW+wave,objects_first_output_id[ADSR_____]+adsr*MAX_OUTPUTS_PER_OBJ+ADSR_SHAPE);

    sub_lfo(ADSR_____,adsr,ADSR_INPUTS_NB,STAR,adsrLfo,adsrLfoFreqCoder,WTRI);                 // adsr launcher lfo/touchB
    
    printf("voice:%u coderFreq:%i frAtt:%i wave:%u Ampl:%u amplAtt:%i\n\n",
        voice,voices[voice].coderFreq,voices[voice].coderFreqAtt,wave,voices[voice].coderWaveAmpl[wave],voices[voice].coderWaveAmplAtt[wave]);    
}

void vsetup(uint8_t v,uint16_t coderFreq,uint16_t coderFreqAtt,uint16_t coderAmp,uint16_t coderAmpAtt,int8_t cr,int16_t crAtt,uint8_t wave,int16_t filterFr,int16_t filterFrAtt,int16_t filterLevAtt,int16_t filterRes,int16_t filterResAtt,uint8_t cga,uint16_t cgaa,uint8_t echo,uint8_t echoAtt,uint8_t revb,uint8_t revbAtt)
{
    setVoiceFreqCoders(&voices[v],coderFreq,coderFreqAtt);
    setVoiceCycleRCoders(&voices[v],cr+RC_TABLES_NB-1,crAtt);
    setVoiceFilter(&voices[v],filterFr,filterFrAtt,filterLevAtt,filterRes,filterResAtt);
    setVoiceAmplCoders(&voices[v],wave,coderAmp,coderAmpAtt);                   // manual level, input level attenuator
    setVoiceAmplCoders(&voices[v],BASIC_WAVES_NB,cga,cgaa);                     // voice gain (these two parameters were ignored)
    setVoiceEchoCoders(&voices[v],echo,echoAtt);
    setVoiceRevbCoders(&voices[v],revb,revbAtt);

    dumpVoices(voices,v);
}

void vsetup(uint8_t v,uint16_t coderFreq,uint16_t coderFreqAtt,uint8_t coderAmp,uint8_t  coderAmpAtt,int8_t cr,int16_t crAtt,uint8_t wave){
    vsetup(v,coderFreq,coderFreqAtt,coderAmp, coderAmpAtt,cr,crAtt,wave,coderFreq+409,0,MAX_CTL_ATT,0,0,1,0,0,0,0,0);   
}

void asetup(uint8_t adsr,uint8_t att,uint8_t dec, uint8_t sus,uint8_t rel, uint8_t lev){
        setAdsrDurCoders(adsr,ADSR_ATT,att,FULL_ATTENUATION_VALUE);
        setAdsrDurCoders(adsr,ADSR_DEC,dec,FULL_ATTENUATION_VALUE);
        setAdsrDurCoders(adsr,ADSR_SUS,sus,FULL_ATTENUATION_VALUE);
        setAdsrDurCoders(adsr,ADSR_REL,rel,FULL_ATTENUATION_VALUE);
        adsrCoderLev[adsr]=lev;setAdsrLev(adsr);
}

void asetup(uint8_t adsr,uint8_t att,uint8_t dec, uint8_t sus,uint8_t rel)
{
    asetup(adsr,att,dec,sus,rel,0);
}

// ================= steps of the tests =====================
// every test goes step by step: a step starts when tbget(tb) becomes true ; before waiting for it, the previous touch must be over (tbget(tb) false)
// at the end of a test, tbget(tb) runs it again ; without it for TEST_END_WAIT_MS, the test ends

uint8_t tb=7;
extern volatile uint32_t millisCounter;

#define TEST_END_WAIT_MS 60000                                  // 1 min

// waits for the next step, the audio keeps running: true when tbget(tb) has been set, false when timeoutMs has gone by without it (0 = no limit)
static bool testStep(uint32_t timeoutMs)
{
    while (tbget(tb)) { fillVoices(); }                               // end of the previous touch
    const uint32_t t0 = millisCounter;
    while (!tbget(tb)) {
        fillVoices();
        if (timeoutMs != 0 && (millisCounter - t0) >= timeoutMs) return false;
    }
    return true;
}

#ifdef TEST_REVERB
// ================= reverb test =====================
// voice REVB_TEST_VOICE plays short notes (an ADSR on its wave level) into the reverb ; a series of settings is tried,
// each one announced on the console, followed by its measures: time of the reverb, peak, decay time, envelope
// the audio keeps running: the test calls fillVoices() all the time, the 1 kHz interrupt runs the ADSR

extern uint8_t           adsrStatus[MAX_ADSR];
extern uint32_t          adsrCurrEch[MAX_ADSR];
extern const uint32_t    yTableQ16[];
extern volatile uint32_t i2sUnderruns;
extern uint16_t          revbTestLog[REVB_TEST_LOG_LEN];
extern volatile uint16_t revbTestN;
extern volatile uint32_t revbTestTimeSum, revbTestTimeMax, revbTestCalls, revbTestClip;

volatile bool testDryMute = false;                              // true: fillVoiceBuffer_mono keeps the sends but does not add the voice to the mix (reverb alone)

#define REVB_TEST_VOICE  0
#define REVB_TEST_ADSR   0
#define REVB_TEST_WAVE   WSIN
#define REVB_TEST_FREQ   220.0f                                 // Hz
#define REVB_TEST_ATT    90                                     // attenuator of the wave input that receives the ADSR (saturation was heard above 98)

struct RevbScenario {
    const char* name;
    uint8_t     wave;                                           // WSIN or WSAW
    uint8_t     dur;                                            // duration step 0..127
    uint8_t     shi;                                            // brightness 0..63
    uint8_t     ret;                                            // return 0..63
    uint16_t    noteMs;                                         // length of the note
    bool        dryMute;                                        // reverb alone
};

static const RevbScenario revbScenarios[] = {
    { "short decay",                       WSIN,   0, 63, 63, 150, false },
    { "decay step 64",                     WSIN,  64, 63, 63, 150, false },
    { "decay step 96",                     WSIN,  96, 63, 63, 150, false },
    { "decay step 96, sawtooth",           WSAW,  96, 63, 63, 150, false },
    { "longest decay",                     WSIN, 127, 63, 63, 150, false },
    { "longest decay, sawtooth",           WSAW, 127, 63, 63, 150, false },
    { "decay 96, brightness 32",           WSIN,  96, 32, 63, 150, false },
    { "decay 96, brightness 0",            WSIN,  96,  0, 63, 150, false },
    { "long note, longest decay",          WSIN, 127, 63, 63, 500, false },
    { "long note, longest decay, return 0",WSIN, 127, 63,  0, 500, false },
    { "reverb alone, decay 96",            WSIN,  96, 40, 63, 150, true  },
    { "reverb alone, decay 96, sawtooth",  WSAW,  96, 40, 63, 150, true  },
};

static uint8_t revbTestWave = REVB_TEST_WAVE;                   // wave that receives the ADSR

static void testRevbWave(uint8_t wave)                          // moves the ADSR to another wave of the test voice
{
    if (wave == revbTestWave) return;
    setVoiceAmplCoders(&voices[REVB_TEST_VOICE], revbTestWave, 0, 0);          // the old wave: level 0, attenuator 0 (its input stays connected, it is muted)
    setVoiceAmplCoders(&voices[REVB_TEST_VOICE], wave, 0, REVB_TEST_ATT);
    obj_connect(objects_first_input_id[VOICE____] + REVB_TEST_VOICE * VOICES_INPUTS_NB + VSPW + wave,
                objects_first_output_id[ADSR_____] + REVB_TEST_ADSR * MAX_OUTPUTS_PER_OBJ + ADSR_SHAPE);
    revbTestWave = wave;
}

static void testWaitMs(uint32_t ms)                             // waits while the audio keeps running
{
    const uint32_t t0 = millisCounter;
    while (millisCounter - t0 < ms) { fillVoices(); }
}

static uint8_t testDurStep(uint32_t ms)                         // smallest ADSR duration step that lasts at least ms (one ADSR tick = 1000/ADSR_SAMPLE_RATE ms)
{
    for (uint8_t n = 0; n < DUR_ECH_NB; n++) {
        if (yTableQ16[n] * (1000 / ADSR_SAMPLE_RATE) >= ms) return n;
    }
    return DUR_ECH_NB - 1;
}

static int testDb10(uint32_t peak)                              // peak of revbTestLog (32767 = full scale) in tenths of dBFS
{
    if (peak == 0) return -999;
    return (int)(200.0f * log10f((float)peak / 32767.0f));
}

static void test_reverb_one(uint8_t k, const RevbScenario& sc)
{
    const float rt  = REVB_RT60_MIN + (REVB_RT60_MAX - REVB_RT60_MIN) * (float)yTableQ16[sc.dur] / (float)yTableQ16[DUR_ECH_NB - 1];
    const uint32_t tailMs = (uint32_t)(rt * 1300.0f) + 500;     // long enough for the -60 dB point

    printf("\n== reverb test %u/%u: %s (%s, decay step %u = %d ms, brightness %u, return %u, note %u ms%s)\n",
           k + 1, (unsigned)(sizeof(revbScenarios) / sizeof(revbScenarios[0])), sc.name, (sc.wave == WSAW) ? "sawtooth" : "sine",
           sc.dur, (int)(rt * 1000.0f), sc.shi, sc.ret, sc.noteMs, sc.dryMute ? ", reverb alone" : "");

    testRevbWave(sc.wave);
    effects_init();                                             // clean lines, echo off
    setRevbCoder(0, sc.dur, 0, sc.shi, 0, sc.ret, 0);
    setVoiceRevbCoders(&voices[REVB_TEST_VOICE], WAVE_LEVEL_STEPS - 1, 0);
    asetup(REVB_TEST_ADSR, 0, 0, testDurStep(sc.noteMs), 12);   // fast attack and decay, sustain = note length, short release
    testDryMute = sc.dryMute;

    const uint32_t under0 = i2sUnderruns;
    const uint32_t irq0 = save_and_disable_interrupts();
    revbTestN = 0; revbTestTimeSum = 0; revbTestTimeMax = 0; revbTestCalls = 0; revbTestClip = 0;
    restore_interrupts(irq0);

    testWaitMs(50);
    const uint16_t noteStart = revbTestN;                       // log entry where the note starts
    const uint32_t irq1 = save_and_disable_interrupts();
    adsrStatus[REVB_TEST_ADSR]  = ADSR_ATT;                     // same as a trigger on the start input
    adsrCurrEch[REVB_TEST_ADSR] = 0;
    restore_interrupts(irq1);
    testWaitMs(sc.noteMs + tailMs);
    testDryMute = false;

    // measures
    const uint32_t n = revbTestN;
    const float bufMs = (float)SAMPLES_PER_BUFFER * 1000.0f / (float)SAMPLE_RATE;
    uint32_t peak = 0, peakAt = 0;
    for (uint32_t i = 0; i < n; i++) { if (revbTestLog[i] > peak) { peak = revbTestLog[i]; peakAt = i; } }
    const uint32_t noteEnd = noteStart + (uint32_t)((float)sc.noteMs / bufMs);
    uint32_t ref = 0;                                           // level at the end of the note: the decay is measured from there
    for (uint32_t i = noteEnd; i < noteEnd + 3 && i < n; i++) { if (revbTestLog[i] > ref) ref = revbTestLog[i]; }

    // decay time: slope of the envelope (peak of every 100 ms) between 5 and 35 dB below the level at the end of the note, extended to 60 dB
    // (a single buffer of a sine can dip far below the mean: the first one under -60 dB would give too short a time)
    const uint32_t step = (uint32_t)(100.0f / bufMs + 0.5f);   // log entries per 100 ms
    int32_t decayMs = -1;
    if (ref > 0) {
        const float refDb = 20.0f * log10f((float)ref);
        float sx = 0, sy = 0, sxx = 0, sxy = 0;
        uint32_t cnt = 0;
        for (uint32_t i = noteEnd; i + step <= n; i += step) {
            uint32_t m = 0;
            for (uint32_t j = i; j < i + step; j++) { if (revbTestLog[j] > m) m = revbTestLog[j]; }
            if (m == 0) break;
            const float d = 20.0f * log10f((float)m) - refDb;   // dB below the end of the note
            if (d < -35.0f) break;
            if (d <= -5.0f) {
                const float x = (float)(i - noteEnd) * bufMs;
                sx += x; sy += d; sxx += x * x; sxy += x * d; cnt++;
            }
        }
        if (cnt >= 3) {
            const float slope = (cnt * sxy - sx * sy) / (cnt * sxx - sx * sx);    // dB per ms, negative
            if (slope < 0.0f) decayMs = (int32_t)(-60.0f / slope);
        }
    }
    const int pk = testDb10(peak);

    printf("   time of the reverb: mean %lu us, max %lu us over %lu buffers ; mix beyond full scale: %lu samples ; i2s underruns: %lu\n",
           (unsigned long)(revbTestCalls ? revbTestTimeSum / revbTestCalls : 0), (unsigned long)revbTestTimeMax, (unsigned long)revbTestCalls,
           (unsigned long)revbTestClip, (unsigned long)(i2sUnderruns - under0));
    fillVoices();
    const int pka = (pk < 0) ? -pk : pk;
    if (peak == 0) printf("   reverb peak: none (no reverb output) ; decay time (slope from -5 to -35 dB): ");
    else           printf("   reverb peak %s%d.%d dBFS at %d ms ; decay time (slope from -5 to -35 dB): ", (pk < 0) ? "-" : "", pka / 10, pka % 10, (int)((float)((int32_t)peakAt - (int32_t)noteStart) * bufMs));
    if (decayMs >= 0) printf("%ld ms (expected %d ms)\n", (long)decayMs, (int)(rt * 1000.0f));
    else              printf("not measured (no reverb, or the decay is too short for the 100 ms steps)\n");
    fillVoices();

    printf("   envelope from the start of the note, every 100 ms (dBFS):\n  ");
    uint8_t col = 0;
    for (uint32_t i = noteStart; i < n; i += step) {
        uint32_t m = 0;
        for (uint32_t j = i; j < i + step && j < n; j++) { if (revbTestLog[j] > m) m = revbTestLog[j]; }
        const int d = testDb10(m);
        if (d <= -999) printf("  -inf"); else printf(" %5d", d / 10);
        if (++col == 10) { col = 0; printf("\n  "); fillVoices(); }
    }
    printf("\n");
}

void test_reverb()
{
    printf("\n========== reverb test: every step starts with tbget(tb) ==========\n");
    const uint8_t v = REVB_TEST_VOICE;
    const uint16_t coderF = calcCoderFreq(REVB_TEST_FREQ);

    // the voice: wave level 0 + ADSR through the attenuator, filter wide open, gain coder 1, no echo
    vsetup(v, coderF, 0, 0, REVB_TEST_ATT, 0, 0, REVB_TEST_WAVE, FILTER_CODE_MAX, 0, MAX_CTL_ATT, 0, 0, 1, 0, 0, 0, WAVE_LEVEL_STEPS - 1, 0);
    revbTestWave = REVB_TEST_WAVE;
    obj_connect(objects_first_input_id[VOICE____] + v * VOICES_INPUTS_NB + VSPW + REVB_TEST_WAVE,
                objects_first_output_id[ADSR_____] + REVB_TEST_ADSR * MAX_OUTPUTS_PER_OBJ + ADSR_SHAPE);
    setAdsrLevCoders(REVB_TEST_ADSR, ADSR_MAX_LEVEL_CODERS, FULL_ATTENUATION_VALUE);

    const uint8_t nb = sizeof(revbScenarios) / sizeof(revbScenarios[0]);
    do {
        for (uint8_t k = 0; k < nb; k++) {
            printf("\n-> tbget(%u): step %u/%u, %s\n", tb, k + 1, nb, revbScenarios[k].name);
            testStep(0);
            test_reverb_one(k, revbScenarios[k]);
        }
        printf("\n-> tbget(%u): run the reverb test again (without it for %lu s, the test ends)\n", tb, (unsigned long)(TEST_END_WAIT_MS / 1000));
    } while (testStep(TEST_END_WAIT_MS));
    printf("========== end of the reverb test ==========\n\n");
}
#endif  // TEST_REVERB

void test_ping_init()
{ 
        //      tb1->adsr3->voice3(w)

        uint8_t adsr=3;
        uint8_t v=3;
        uint8_t b=1;
        uint16_t coderf=1535;       // 1535=220Hz
        uint8_t w=WSAW;

        obj_connect(objects_first_input_id[ADSR_____]+adsr*ADSR_INPUTS_NB+STAR,objects_first_output_id[TBUTTON__]+b*MAX_OUTPUTS_PER_OBJ+TOUCH_BUTTON);
        obj_connect(objects_first_input_id[VOICE____]+v*VOICES_INPUTS_NB+VSPW+w,objects_first_output_id[ADSR_____]+adsr*MAX_OUTPUTS_PER_OBJ+ADSR_SHAPE);
        asetup(adsr,1,84,0,0,0);    //void asetup(uint8_t adsr,uint8_t att,uint8_t dec, uint8_t sus,uint8_t rel, uint8_t lev)
        //vsetup(v,coderf,0,0,120,0,0,WSIN,coderf+409,0,MAX_CTL_ATT,0,0,1,0,63,0,0,0);
        //setEchoCoder(0,100,0,50,0,63,0);setRevbCoder(0,0,0,0,0,0,0);
        vsetup(v,coderf,0,0,120,0,0,w,coderf+818,0,MAX_CTL_ATT,0,0,1,0,0,0,0,0);    // void vsetup(uint8_t v,uint16_t coderFreq,uint16_t coderFreqAtt,uint16_t coderAmp,uint16_t coderAmpAtt,int8_t cr,int16_t crAtt,uint8_t wave,int16_t filterFr,int16_t filterFrAtt,int16_t filterLevAtt,int16_t filterRes,int16_t filterResAtt,uint8_t cga,uint16_t cgaa,uint8_t echo,uint8_t echoAtt,uint8_t revb,uint8_t revbAtt)
        //setRevbCoder(0,100,0,63,0,63,0);setEchoCoder(0,0,0,0,0,0,0);
}


void testSetup()
{
    test_ping_init();return;

#ifdef TEST_REVERB
    test_reverb();
#endif
}


/*

// Ids of lfm inputs/outputs are read from the tables built at init,
// so the layout (input-major or lfm-major) is never assumed here.
static inline int16_t lfmInId(uint8_t lfm,uint8_t inp){return lfm_ctl_input_id[inp][lfm];}
static inline int16_t lfmOutId(uint8_t lfm){return lfm_ctl_output_id[LMUXO][lfm];}

void sound3(uint8_t voice,uint8_t vwave,uint16_t coderFreq,uint8_t coderAttF,uint8_t coderAmp,uint8_t coderAmpAtt,
        uint8_t coderCr,uint8_t crAtt,
        int8_t adsr,int8_t sourceAdsr,
        int8_t lfm,int8_t lfm1,uint8_t voiceAmpLfoLfmInput,uint8_t voiceFreqLfoLfmInput,uint8_t voiceFreqAdsrLfmInput,
        int8_t voiceAmpLfo,uint16_t ampLfoFreq,
        int8_t voiceFreqLfo,uint16_t freqLfoFreq
    )
{
    // config voice
    vsetup(voice,coderFreq,coderAttF,coderAmp,coderAmpAtt,coderCr,crAtt,vwave);

    // connect source to adsr (sourceAdsr<0 : lfo number -1-sourceAdsr ; otherwise touch button number)
    uint8_t sourceType=TBUTTON__;
    int8_t sourceNum=sourceAdsr;
    uint8_t waveF=0;
    if(sourceAdsr<0){
        sourceNum=-1-sourceAdsr;
        sourceType=LFO______;
        waveF=WSIN;
    }
    obj_connect(objects_first_input_id[ADSR_____]+adsr*ADSR_INPUTS_NB+STAR,
                objects_first_output_id[sourceType]+sourceNum*MAX_OUTPUTS_PER_OBJ+waveF);

    // config lfo amp
    if(voiceAmpLfo>=0){
        setLfosFrCoders(voiceAmpLfo,ampLfoFreq,0);
        setLfosFrequency(voiceAmpLfo);
    }

    if(lfm>=0){
        // connect lfm0 to voice:wave amp input
        obj_connect(objects_first_input_id[VOICE____]+voice*VOICES_INPUTS_NB+VSPW+vwave,lfmOutId(lfm));
        // connect adsr to lfm0:0 genAmp input
        uint8_t adsrLfmInput=0;
        setLfmCoders(lfm,adsrLfmInput,lfmCoder[adsrLfmInput][lfm],70);
        obj_connect(lfmInId(lfm,adsrLfmInput),objects_first_output_id[ADSR_____]+adsr*MAX_OUTPUTS_PER_OBJ+ADSR_SHAPE);
        if(voiceAmpLfo>=0){
            // connect lfo amp to lfm0:1
            obj_connect(lfmInId(lfm,voiceAmpLfoLfmInput),objects_first_output_id[LFO______]+voiceAmpLfo*MAX_OUTPUTS_PER_OBJ+LSIN);
            // config lfo modulation
            setLfmCoders(lfm,voiceAmpLfoLfmInput,255,120);
        }

        if(lfm1>=0){
            // connect lfm1 to voice freq
            obj_connect(objects_first_input_id[VOICE____]+voice*VOICES_INPUTS_NB+VFRQ,lfmOutId(lfm1));
            // lfm1 in0 base wide open ; no input
            setLfmCoders(lfm1,0,MAX_CTL_ATT-1,0);
            if(voiceAmpLfo>=0){
                // connect lfo to lfm1:voiceFreqLfoLfmInput (same lfo as amp)
                obj_connect(lfmInId(lfm1,voiceFreqLfoLfmInput),objects_first_output_id[LFO______]+voiceAmpLfo*MAX_OUTPUTS_PER_OBJ+LSIN);
                // lfm1 lfo input att wide open
                setLfmCoders(lfm1,voiceFreqLfoLfmInput,0,MAX_CTL_ATT/2-1);
            }
            // connect adsr to lfm1:voiceFreqAdsrLfmInput
            obj_connect(lfmInId(lfm1,voiceFreqAdsrLfmInput),objects_first_output_id[ADSR_____]+adsr*MAX_OUTPUTS_PER_OBJ+ADSR_SHAPE);
            // lfm1 adsr input att wide open
            setLfmCoders(lfm1,voiceFreqAdsrLfmInput,0,MAX_CTL_ATT/2-1);
        }
    }
    else{
        if(voiceFreqLfo>=0){
            setLfosFrCoders(voiceFreqLfo,freqLfoFreq,0);
            setLfosFrequency(voiceFreqLfo);
            obj_connect(objects_first_input_id[VOICE____]+voice*VOICES_INPUTS_NB+VFRQ,
                        objects_first_output_id[LFO______]+voiceFreqLfo*MAX_OUTPUTS_PER_OBJ+LTRI);
        }

        // CX (voice)SIP (adsr)
        printf("Amp ctl adsr:%u ",adsr);
        obj_connect(objects_first_input_id[VOICE____]+voice*VOICES_INPUTS_NB+VSPW+vwave,
                    objects_first_output_id[ADSR_____]+adsr*MAX_OUTPUTS_PER_OBJ+ADSR_SHAPE);

        printf("voice:%u coderFreq:%i frAtt:%i wave:%u Ampl:%u amplAtt:%i\n\n",
            voice,voices[voice].coderFreq,voices[voice].coderFreqAtt,vwave,voices[voice].coderWaveAmpl[vwave],voices[voice].coderWaveAmplAtt[vwave]);
    }
}

void old_test()
{
    // 1096 110Hz // 1936 440Hz // 2355 880Hz    @420/octave demi_ton 35

    #define VOICE0 0x80
    #define VOICE1 0x40
    #define VOICE2 0x20
    #define VOICE3 0x10
    uint8_t action=VOICE3;  //|VOICE0|VOICE1|VOICE2;

    printf("\n========== test-setup 0x%02X ==========\n",action);    

    if((action&VOICE0) != 0){  
        
        // config voice 0    
        uint8_t  voice=0;
        uint8_t  vwave=WTRI;
        uint16_t coderFreq=800; 
        uint16_t coderFil=1500;           
        uint8_t  coderAttF=125;
        uint8_t  coderAmp=15;
        uint8_t  coderAttAmp=80;         
        int8_t   cr=10;
        uint8_t  crAtt=64;
        uint8_t  freqLfo=0;
        uint16_t freqLfoCoder=3392;         // 5Hz  
        uint8_t  adsr=0;
        int8_t   adsrLfo=2;    
        uint32_t adsrLfoCoder=1384;         // 1384 6sec // 1790 3sec
        uint8_t  crLfo=4;
        uint16_t crLfoCoder=1820;
        uint8_t  lfm=0;
        uint8_t  lfmInp0=0;
        uint8_t  lfmInp1=1;
        uint8_t  lfmInp2=2;
        uint8_t  filtLfo=7; 

        voiceConfig(voice,vwave,coderFreq,coderAttF,freqLfo,freqLfoCoder,coderAmp,coderAttAmp,adsr,adsrLfo,adsrLfoCoder,cr,crAtt);
        
        asetup(adsr,6,28,12,96);

        voices[voice].coderCycleRAtt=200;                           // input atten
        sub_lfo(VOICE____,0,VOICES_INPUTS_NB,VCR_,4,1700,WTRI);                      // 1700=3.5s 

        setVoiceFilter(&voices[0],coderFil,100,255,0,255);
        //setLfosFreq(filtLfo,1790);
        //setLfosCycleR(filtLfo,0,0);

        //obj_connect(objects_first_input_id[VOICE____]+1*VOICES_INPUTS_NB+VFIL,objects_first_output_id[LFO______]+filtLfo*MAX_OUTPUTS_PER_OBJ+LTRI);        
        
        printf("\n");
    }

    // config voice 1
    if((action&VOICE1) != 0){
        
        uint8_t  v=1;
        uint8_t  filtLfo=6;
        uint8_t  adsr=1;
        //sound3(1,WSIN,1740,12,8,60,1,adsr,-3,0,0,0,0,0,-1,3499,1,3499);
        voiceConfig(v,WSIN,1740,12,1,3499,0,30,adsr,3,1200,1,0);
        setVoiceAmplCoders(&voices[v],5,30,0);
        asetup(adsr,96,60,11,90);

        setVoiceFilter(&voices[1],2000,100,150,0,255);
        
        setLfosFrCoders(filtLfo,1384,0);
        setLfosFrequency(filtLfo);
        setLfosCycleR(filtLfo,0,0);     //MAXCODER_RC/2,0);

        obj_connect(objects_first_input_id[VOICE____]+1*VOICES_INPUTS_NB+VFIL,objects_first_output_id[LFO______]+filtLfo*MAX_OUTPUTS_PER_OBJ+LTRI);
    }

    //  config voice2 (touch button 0)
    if((action&VOICE2) != 0){ 
        //      tb0->adsr->mux0inp0->sinpower2
        //      lfo5->mux1inp1 coder[1][1]=32563
        //      mux1->mux0inp1
        uint8_t adsr=2;
        sound3(2,WSIN,2300,70,0,30,1,0,adsr,0,0,1,1,1,2,5,3499,5,3499);
        asetup(adsr,127,32,16,127);
    }

    if((action&VOICE3) != 0){ 
        //      tb1->adsr->mux0inp0->sinpower2
        //      lfo5->mux1inp1 coder[1][1]=32563
        //      mux1->mux0inp1
        uint8_t adsr=3;
        uint8_t v=3;
        uint8_t b=1;
        uint16_t coderf=1500;

        obj_connect(objects_first_input_id[ADSR_____]+adsr*ADSR_INPUTS_NB+STAR,objects_first_output_id[TBUTTON__]+b*MAX_OUTPUTS_PER_OBJ+TOUCH_BUTTON);
        obj_connect(objects_first_input_id[VOICE____]+v*VOICES_INPUTS_NB+VSPW+WSIN,objects_first_output_id[ADSR_____]+adsr*MAX_OUTPUTS_PER_OBJ+ADSR_SHAPE);
        asetup(adsr,1,50,1,40);
        //vsetup(v,coderf,0,0,120,0,0,WSIN,coderf+409,0,MAX_CTL_ATT,0,0,1,0,63,0,0,0);
        //setEchoCoder(0,100,0,50,0,63,0);setRevbCoder(0,0,0,0,0,0,0);
        vsetup(v,coderf,0,0,120,0,0,WSIN,coderf+409,0,MAX_CTL_ATT,0,0,1,0,0,0,63,0);
        setRevbCoder(0,100,0,63,0,63,0);setEchoCoder(0,0,0,0,0,0,0);
    }

    if(action==0){
        uint8_t     voice=0;
        uint8_t     vwave=WSIN;
        int8_t      cr=0;
        uint8_t     crAtt=0;
        uint16_t    coderF=1943;
        uint8_t     coderAttF=70;
        uint8_t     coderA=10;
        uint8_t     coderAttA=0;
        uint8_t     lfo=0;
        uint8_t     lwave=WSIN;

        setLfosFrCoders(lfo,(uint16_t)1819,0); // 0.375hz ; 2239 0.75hz ; 2659 1.5hz ; 3079 3hz ; 3499 6hz
        setLfosFrequency(lfo);
        setLfosCycleR(lfo,0,0);

        vsetup(voice,coderF,coderAttF,coderA,coderAttA,cr,crAtt,vwave);

        setVoiceFilter(&voices[voice],coderF,100,255,0,255);
        //obj_connect(objects_first_input_id[VOICE____]+voice*VOICES_INPUTS_NB+VFIL,objects_first_output_id[LFO______]+lfo*MAX_OUTPUTS_PER_OBJ+lwave);

        //obj_connect(objects_first_input_id[VOICE____]+voice*VOICES_INPUTS_NB+VFRQ,objects_first_output_id[LFO______]+lfo*MAX_OUTPUTS_PER_OBJ+lwave);        
    }
 
    printf("========================================\n");

//pwm_timer_1khz_enable(false);while(1){}
//disp_lfm(255,"testSetup");
}


// **** test voice ****

        Voice*      testV;
        uint8_t     voice=0;
        uint8_t     wave=WSIN;
        
        uint16_t    coderF=calcCoderFreq(440);
        uint16_t    coderFAtt=0;        
        int8_t      cr=0;
        uint8_t     crAtt=0;

        int16_t     coderFilterF=coderF+409;
        int16_t     coderFilterFAtt=0;
        int16_t     coderFilterLevAtt=0;
        int16_t     coderFilterRes=0;
        int16_t     coderFilterResAtt=0;

        uint16_t    coderWA[BASIC_WAVES_NB];
        uint16_t    coderWAA[BASIC_WAVES_NB];

        uint16_t    coderGA=1;
        uint16_t    coderGAA=0;

void testVoiceInit(uint8_t vv,uint8_t w)
{
        voice=vv;
        testV=&voices[voice];                           
        wave=w;
        coderF=calcCoderFreq(440);
        coderFAtt=0;        
        cr=0;
        crAtt=0;

        coderFilterF=coderF+409;
        coderFilterFAtt=0;
        coderFilterLevAtt=MAX_CTL_ATT-1;
        coderFilterRes=252;
        coderFilterResAtt=0;

        for(uint8_t w=0;w<BASIC_WAVES_NB;w++){coderWA[w]=0;}
        coderWA[wave]=WAVE_LEVEL_STEPS/3;

        for(uint8_t w=0;w<BASIC_WAVES_NB;w++){coderWAA[w]=0;}
        coderWAA[wave]=0;

        coderGA=1;
        coderGAA=0;
        
        vsetup(voice,coderF,coderFAtt,coderWA[wave],coderWAA[wave],cr,crAtt,wave,coderFilterF,coderFilterFAtt,coderFilterLevAtt,coderFilterRes,coderFilterResAtt,coderGA,coderGAA,63,0,0,0); 
}

void testVoice()
{ 
            Voice* v=testV;

            fillVoices();

            if( 
                   v->coderFreq             != coderF
                || v->coderFreqAtt          != coderFAtt
                || v->coderCycleR           != cr
                || v->coderCycleRAtt        != crAtt
                || v->coderFilterFreq       !=coderFilterF
                || v->coderFilterFreqAtt    != coderFilterFAtt
                || v->coderFilterLevAtt     != coderFilterLevAtt
                || v->coderFilterRes        != coderFilterRes
                || v->coderFilterResAtt     != coderFilterResAtt
                || v->coderWaveAmpl[0]      != coderWA[0]
                || v->coderWaveAmpl[1]      != coderWA[1]
                || v->coderWaveAmpl[2]      != coderWA[2]
                || v->coderWaveAmpl[3]      != coderWA[3]
                || v->coderWaveAmpl[4]      != coderWA[4]
                || v->coderWaveAmpl[5]      != coderWA[5]
                || v->coderGenAmpl          != coderGA
                || v->coderGenAmplAtt       != coderGAA
            )
            {
                dumpVoices(v,voice);

                coderF                  = v->coderFreq;         
                coderFAtt               = v->coderFreqAtt;      
                cr                      = v->coderCycleR;
                crAtt                   = v->coderCycleRAtt;
                coderFilterF            = v->coderFilterFreq;
                coderFilterFAtt         = v->coderFilterFreqAtt;
                coderFilterLevAtt       = v->coderFilterLevAtt;
                coderFilterRes          = v->coderFilterRes;
                coderFilterResAtt       = v->coderFilterResAtt;
                coderWA[0]              = v->coderWaveAmpl[0];
                coderWA[1]              = v->coderWaveAmpl[1];
                coderWA[2]              = v->coderWaveAmpl[2];
                coderWA[3]              = v->coderWaveAmpl[3];
                coderWA[4]              = v->coderWaveAmpl[4];
                coderWA[5]              = v->coderWaveAmpl[5];
                coderGA                 = v->coderGenAmpl;
                coderGAA                = v->coderGenAmplAtt;
            }
}
*/