#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "const.h"
#include "util.h"
#include "miscControls.h"
#include "input_tables_management.h"
#include "frequences.h"
#include "hardware/sync.h"
#include "hardware/dma.h"

extern Voice voices[MAX_VOICES];

/* ****** adsr ****** */

uint16_t adsrCoderAtt[MAX_ADSR];
uint16_t adsrCoderDec[MAX_ADSR];
uint16_t adsrCoderSus[MAX_ADSR];
uint16_t adsrCoderRel[MAX_ADSR];
uint16_t adsrCoderLev[MAX_ADSR];
uint16_t adsrCoderAttAtt[MAX_ADSR];
uint16_t adsrCoderDecAtt[MAX_ADSR];
uint16_t adsrCoderSusAtt[MAX_ADSR];
uint16_t adsrCoderRelAtt[MAX_ADSR];
uint16_t adsrCoderLevAtt[MAX_ADSR];
uint8_t  adsrLev[MAX_ADSR];                                 // effective sustain level index (coder + modulation), 0..ADSR_MAX_LEVEL_CODERS
uint8_t  adsrStatus[MAX_ADSR];
uint32_t adsrDurAtt[MAX_ADSR];
uint32_t adsrDurDec[MAX_ADSR];
uint32_t adsrDurSus[MAX_ADSR];
uint32_t adsrDurRel[MAX_ADSR];
uint32_t adsrCurrEch[MAX_ADSR];
int16_t  adsr_ctl_input_id[MAX_ADSR][ADSR_INPUTS_NB];
int16_t  adsr_ctl_output_id[MAX_ADSR][MAX_OUTPUTS_PER_OBJ];
int32_t  adsrScopeBufReal[MAX_ADSR*ADSR_SCOPE_BUFFER_LEN];  // real values
uint16_t adsrScopeBufPtr[MAX_ADSR];

uint32_t    adsrTime=0;
uint32_t    adsrTimingInterval=1000/ADSR_SAMPLE_RATE;

bool     adsrScopeDisp[MAX_ADSR];

/* ******* touchButtons ******* */

int16_t  tbut_ctl_output_id[MAX_TBUT][MAX_OUTPUTS_PER_OBJ];

/* *******  lf_mixers  ******** */

uint16_t lfmCoder[LFM_INPUTS_NB][MAX_LFM];                  // 8 bits basic coder value before 16 bits extension
uint16_t lfmCoderAtt[LFM_INPUTS_NB][MAX_LFM];               // 8 bits input attenuator value
int16_t  intermediateOutputValues[MAX_LFM];                 // outputValue before genAtt Attenuation
int32_t  lfmGenAttValue[MAX_LFM];                           // outputValue Attenuation
int16_t  lfmOutputValues[MAX_LFM];
uint8_t  lfmInputType[LFM_INPUTS_NB][MAX_LFM];
int16_t  lfm_ctl_input_id[LFM_INPUTS_NB][MAX_LFM];
int16_t  lfm_ctl_output_id[MAX_OUTPUTS_PER_OBJ][MAX_LFM];
uint8_t  lfmCh[]={'_','\\','/'};
int32_t  lfmScopeBufReal[MAX_LFM*LFM_SCOPE_BUFFER_LEN];
uint16_t lfmScopeBufPtr[MAX_LFM];
bool     lfmScopeDisp[MAX_LFM];

uint32_t    lfmTime=0;                     
uint32_t    lfmScopeTimingInterval=1000/LFM_SAMPLE_RATE;

extern uint16_t* lfmVar[];

static_assert(sizeof(lfmCoder[0][0]) == 2, "CODER_BASE_SHIFT assumes lfmCoder is 16-bit; update the shift if the type changes or create a new parameter");

/* ****** reverb/echo ****** */

uint16_t revDurCoder[MAX_REV];
uint16_t revShiCoder[MAX_REV];
uint16_t revRetCoder[MAX_REV];
uint16_t revDurCoderAtt[MAX_REV];
uint16_t revShiCoderAtt[MAX_REV];
uint16_t revRetCoderAtt[MAX_REV];

int16_t bufReverb[MAX_REV][REV_BUF_LEN];

int16_t rev_ctl_input_id[REV_INPUTS_NB][MAX_REV];

// ************************* echo ****************************

uint16_t echoDurCoder[MAX_ECHO];
uint16_t echoInjCoder[MAX_ECHO];
uint16_t echoRetCoder[MAX_ECHO];
uint16_t echoDurCoderAtt[MAX_ECHO];
uint16_t echoInjCoderAtt[MAX_ECHO];
uint16_t echoRetCoderAtt[MAX_ECHO];
        
int16_t echo_ctl_input_id[ECHO_INPUTS_NB][MAX_ECHO];

int16_t       bufEcho[MAX_ECHO][ECHO_BUF_LEN] __attribute__((aligned(4)));    // delay line: the last second of the echo input, at 22050 Hz
int32_t       echoBus[MAX_ECHO][ECHO_STEPS];                    // sum of the voices sent to the echo during the current audio buffer, at 22050 Hz
volatile bool echoBusUsed[MAX_ECHO];                            // set by fillVoiceBuffer_mono when a voice has sent something

// values computed by setEcho (menu or 1 kHz interrupt), read once per audio buffer by echoProcess
static volatile int32_t echoDelay[MAX_ECHO];                    // delay in steps (22050 Hz)
static volatile int32_t echoFb[MAX_ECHO];                       // feedback, Q15
static volatile int32_t echoRet[MAX_ECHO];                      // return level, 0..32767

static EchoState echoSt[MAX_ECHO];
static int       echoDmaChan = -1;
static uint32_t  echoZero = 0;                                  // source of the DMA clear: static, it must outlive the transfer


/* *******  extern  ******** */

extern volatile uint32_t millisCounter;
extern bool             setupComplete;
extern uint16_t         amplLevel[];
extern int16_t          ctl_input_val[TOTAL_INPUTS_ID_REAL];
extern int16_t          ctl_output_id_chain[];
extern uint16_t         waveLevel[];
extern const uint32_t   yTableQ16[];
extern Voice            voices[MAX_VOICES];



void adsrInit()                     // (id ptrs dans inits_objects_inputs/outputs)
{
    printf("%u adsr init\n",MAX_ADSR);
    for(uint8_t a=0;a<MAX_ADSR;a++){

        setAdsrDurCoders(a,ADSR_ATT,6,FULL_ATTENUATION_VALUE);
        setAdsrDurCoders(a,ADSR_DEC,28,FULL_ATTENUATION_VALUE);
        setAdsrDurCoders(a,ADSR_SUS,12,FULL_ATTENUATION_VALUE);
        setAdsrDurCoders(a,ADSR_REL,96,FULL_ATTENUATION_VALUE);
        setAdsrLevCoders(a,ADSR_MAX_LEVEL_CODERS,FULL_ATTENUATION_VALUE);

        adsrStatus[a]=ADSR_OFF;
        adsrCurrEch[a]=0;
        adsrScopeBufPtr[a]=0;

    }
}

#define ONE_Q16     65536
#define RC_SAMPLES_NB  2048
#define P16 (1<<16) 

extern const uint32_t yTableQ16[DUR_ECH_NB] = {
//1,2,3,4,5,7,8,10,11,13,15,18,21,24,27,31,35,39,44,49,55,62,69,76,84,93,103,114,125,137,151,165,181,197,215,235,256,278,302,328,356,
//386,418,452,488,527,569,614,661,712,766,824,886,952,1021,1096,1175,1259,1348,1443,1544,1651,1765,1886,2014,2149,2293,2445,2606,2777,
//2957,3148,3350,3564,3790,4028,4281,4547,4828,5124,5438,5768,6116,6483,6871,7279,7709,8162,8639,9141,9669,10226,10811,11427,12074,
//12755,13471,14223,15014,15845,16717,17634,18596,19606,20666,21778,22946,24170,25454,26801,28213,29693,31245,32870,34574,36358,
//38227,40184,42233,44379,46624,48974,51434,54007,56698,59514,62458,65532
1,2,2,3,4,4,5,6,7,8,9,10,11,12,14,15,17,18,20,22,24,26,29,31,34,36,39,42,46,49,53,57,61,66,
71,76,81,86,92,98,105,112,119,127,135,143,152,162,172,182,193,204,216,229,242,256,271,286,302,318,336,354,373,394,414,436,459,483,508,535,
562,591,620,652,684,718,753,790,829,869,911,954,999,1047,1096,1147,1201,1256,1314,1374,1437,1502,1570,1640,1713,1789,1868,1951,2036,2125,
2217,2313,2412,2515,2622,2734,2849,2969,3093,3222,3356,3495,3638,3788,3942,4103,4269,4441,4620,4804,4996,5195,5400,5613,5834,6062,6298,6543
};

uint32_t stepTableQ16[DUR_ECH_NB];      // coefficient RC par durée (Q16)
// courbe RC, 0 à ~65532 (échelle interne ; conversion vers l'échelle int16 faite une seule fois après le switch de adsrHandler)
uint16_t rcCurve[RC_SAMPLES_NB];        
const uint32_t stepMinQ16 = 32768;                    // 0.5 en Q16
const uint32_t stepMaxQ16 = (RC_SAMPLES_NB << 16);    // RC_SAMPLES_NB en Q16

void fillDur(void)
{
    // stepTableQ16 : transfo des DUR_ECH_NB valeurs linéaires en courbe +-log pour ergonomie coder
    // production yTableQ16 voir tableau excel
    const uint32_t yMax = yTableQ16[DUR_ECH_NB-1];      // 65532
    for (uint32_t d = 0; d < DUR_ECH_NB; d++) {
        uint32_t y = yTableQ16[d];
        if (y == 0) y = 1;

        // step en Q16 = (RC_SAMPLES_NB / y) en Q0
        // donc stepQ16 = (RC_SAMPLES_NB << 16) / y
        stepTableQ16[d] = ((uint32_t)RC_SAMPLES_NB << 16) / y;
        //printf("%u %u\n",d,stepTableQ16[d]);
    }  


// rcCurve : approximation tension condensateur RC
    
// formule générale pour obtenir la table 
//  1) kQ16 détermine le creux
//  k = 1-(1-taux)^(1/RC_SAMPLES_NB)
//  kQ16 = arrondi (k*65536)
//  ex: taux = 0.9 ; RC_SAMPLES_NB = 2048
//  k = 1-0.1^(1/2048) = 0.0011237
//  kQ16 = 73.64 soit 74
//  ex: taux = 0.91 ; RC_SAMPLES_NB = 2048
//  k = 1-0.09^(1/2048) = 0.001175
//  kQ16 = 77
//  ex: taux = 0.93 ; RC_SAMPLES_NB = 2048
//  k = 1-0.07^(1/2048) = 0.001297
//  kQ16 = 85
//  2) ONE_Q16_XX le taux de la courbe parcouru 
//  ONE_Q16=65536 ; ONE_Q16_xx = ONE_Q16 / taux
//  ex: taux = 0.9 ; ONE_Q16_09 = 65536 / 0.9 = 72817
//  ex: taux = 0.91 ; ONE_Q16_091 = 65536 / 0.91 = 72017 ajusté à 72440
//  ex: taux = 0.93 ; ONE_Q16_093 = 65536 / 0.93 = 70469 ajusté à 70858
//  la valeur finale peut être ajustée pour que le dernier step amène aussi près que possible de 65536 (voir tableau excel)
 
    #define ONE_Q16_90  72440 // 65536 / 0.905 ; 65536 * 1/0.90   
    #define ONE_Q16_93  70858
    #define ONE_Q16_RC ONE_Q16_93
    #define KQ16_90 77   // Pour coeff =0.90 et 2048 samples : 𝑘 = 1-0,091/2048≈0,0011756 𝑘𝑄16≈0,0011756*65536≈77
    #define KQ16_93 85
    #define KQ16 KQ16_93

    uint32_t yQ16 = 0;    

    for (int i = 0; i < RC_SAMPLES_NB; i++)
    {
        uint32_t diff = ONE_Q16_RC - yQ16;
        uint32_t dy   = (uint32_t)(((uint64_t)diff * KQ16) >> 16);
        yQ16 += dy;
        if (yQ16 > ONE_Q16_RC) yQ16 = ONE_Q16_RC;

        uint32_t v = (uint32_t)(((uint64_t)yQ16 * 65535) >> 16);
        if(v > 65535){system_error("overflow rcCurve", v);}
        rcCurve[i] = (uint16_t)v;
        //printf("%i %u\n",i,rcCurve[i]);
    }
}

// shift applied to the 16-bit input value before it is added to the coder (the input is then attenuated by coderXxxAtt/256):
// 15 - log2(number of steps of the target scale): 8 sweeps the whole range of the durations (128 steps) with a full-scale input and a full attenuator, 9 sweeps half of it
#define ADSR_ATT_SHIFT  9       // same values as before the rewrite
#define ADSR_DEC_SHIFT  9
#define ADSR_SUS_SHIFT  3
#define ADSR_REL_SHIFT  3
#define ADSR_LEV_SHIFT  10      // level: 32 steps (the old shift of 3 was far too large for this scale)

// compute: the duration of a stage (step of the reading of the rc curve), from the stored coders and the stored value of its input
void __not_in_flash_func(setAdsrDur)(uint8_t adsr,uint8_t adsrStage)
{
    uint32_t* vv;
    int32_t   coder,att;
    int16_t   id;
    uint8_t   shift;

    switch(adsrStage){
        case ADSR_ATT:vv=&adsrDurAtt[adsr];id=adsr_ctl_input_id[adsr][ATTK];coder=adsrCoderAtt[adsr];att=adsrCoderAttAtt[adsr];shift=ADSR_ATT_SHIFT;break;
        case ADSR_DEC:vv=&adsrDurDec[adsr];id=adsr_ctl_input_id[adsr][DECA];coder=adsrCoderDec[adsr];att=adsrCoderDecAtt[adsr];shift=ADSR_DEC_SHIFT;break;
        case ADSR_SUS:vv=&adsrDurSus[adsr];id=adsr_ctl_input_id[adsr][SUST];coder=adsrCoderSus[adsr];att=adsrCoderSusAtt[adsr];shift=ADSR_SUS_SHIFT;break;
        case ADSR_REL:vv=&adsrDurRel[adsr];id=adsr_ctl_input_id[adsr][RELE];coder=adsrCoderRel[adsr];att=adsrCoderRelAtt[adsr];shift=ADSR_REL_SHIFT;break;
        default:return;
    }
    *vv=stepTableQ16[ctlCombine(coder,att,ctl_input_val[id],shift,0,DUR_ECH_NB-1)];
}

// load the coders of a stage, then compute its duration (same model as setVoiceFilter)
void __not_in_flash_func(setAdsrDurCoders)(uint8_t adsr,uint8_t adsrStage,uint16_t coder,uint16_t coderAtt)
{
    const uint32_t irq=save_and_disable_interrupts();                       // the interrupt that updates the inputs computes the same value: no interleaving
    switch(adsrStage){
        case ADSR_ATT:adsrCoderAtt[adsr]=coder;adsrCoderAttAtt[adsr]=coderAtt;break;
        case ADSR_DEC:adsrCoderDec[adsr]=coder;adsrCoderDecAtt[adsr]=coderAtt;break;
        case ADSR_SUS:adsrCoderSus[adsr]=coder;adsrCoderSusAtt[adsr]=coderAtt;break;
        case ADSR_REL:adsrCoderRel[adsr]=coder;adsrCoderRelAtt[adsr]=coderAtt;break;
        default:restore_interrupts(irq);return;
    }
    setAdsrDur(adsr,adsrStage);
    restore_interrupts(irq);
}

// compute: the sustain level index (0..ADSR_MAX_LEVEL_CODERS), from the stored coders and the stored value of the level input
void __not_in_flash_func(setAdsrLev)(uint8_t adsr)
{
    adsrLev[adsr]=(uint8_t)ctlCombine(adsrCoderLev[adsr],adsrCoderLevAtt[adsr],ctl_input_val[adsr_ctl_input_id[adsr][LEVE]],ADSR_LEV_SHIFT,0,ADSR_MAX_LEVEL_CODERS);
}

void __not_in_flash_func(setAdsrLevCoders)(uint8_t adsr,uint16_t coder,uint16_t coderAtt)
{
    const uint32_t irq=save_and_disable_interrupts();
    adsrCoderLev[adsr]=coder;
    adsrCoderLevAtt[adsr]=coderAtt;
    setAdsrLev(adsr);
    restore_interrupts(irq);
}

uint16_t adsrNext(uint32_t* currEchQ16,uint32_t stepQ16)    // production indice suivant dans rcCurve selon dur
{
    // avance
    uint32_t next = *currEchQ16 + stepQ16;

    // clamp fin de courbe
    uint32_t maxQ16 = ((RC_SAMPLES_NB-1) << 16);
    if (next >= maxQ16){next = maxQ16;}

    *currEchQ16 = next;

    // conversion Q16 → index entier
    return (uint16_t)(next >> 16);
}

#define ADSR_RETRIGGER_FROM_LEVEL 1     // 1: a trigger that comes during a cycle starts the attack from the level reached (no sudden drop, no click) ; 0: always from 0
static uint16_t adsrLastOut[MAX_ADSR];  // last output of every envelope, on the internal 0..65535 scale
static uint32_t adsrAttStart[MAX_ADSR]; // level from which the current attack starts

void __not_in_flash_func(adsrHandler)()
{
    if((millisCounter-adsrTime)>=adsrTimingInterval){
        adsrTime=millisCounter;
//uint32_t ints = save_and_disable_interrupts();gpio_put(TST_PIN,1);
        for(uint8_t a=0;a<MAX_ADSR;a++)
        {
            int16_t   bol=0;
            uint16_t  ov_=0;            // analog level output value
            uint32_t  ov0;
            uint32_t  o32;
            uint8_t*  as=&adsrStatus[a];
            uint16_t* ap=&adsrScopeBufPtr[a];
            if (__builtin_expect(*as != ADSR_OFF, 0)) {

                bol=0x7fff;
                const uint32_t lev=(uint32_t)amplLevel[adsrLev[a]]<<1;   // the stages work on the 0..65535 scale (halved once, at the end of the stages): the level must be on this scale too
                uint32_t* ce=&adsrCurrEch[a];            
                uint32_t  cx=*ce>>16;   // prev currEch  
                
                switch(*as){
                    case ADSR_ATT:
                        if(*ce==0){adsrAttStart[a]=ADSR_RETRIGGER_FROM_LEVEL?adsrLastOut[a]:0;}   // first tick of an attack (every trigger sets *ce to 0)
                        cx=adsrNext(ce,adsrDurAtt[a]);
                        o32=adsrAttStart[a]+(((uint32_t)P16-adsrAttStart[a])*rcCurve[cx])/(uint32_t)P16;
                        ov_=(uint16_t)o32;

                        if (cx == RC_SAMPLES_NB-1) { *ce = 0; *as = ADSR_DEC;}
                        break;

                    case ADSR_DEC:
                        // valeurs 1-x
                        cx=adsrNext(ce,adsrDurDec[a]);
                        ov0=rcCurve[cx];
                        o32=(uint32_t)P16-(((uint32_t)P16-lev)*ov0)/(uint32_t)P16;
                        ov_=(uint16_t)((o32>65535u)?65535u:o32);            // with a level near the top the result can reach 65536: it must not wrap to 0

                        if (cx == RC_SAMPLES_NB-1) {*ce = 0; *as = ADSR_SUS;}                        
                        break;

                    case ADSR_SUS:
                        cx=adsrNext(ce,adsrDurSus[a]);  // timing management                    
                        ov_=(uint16_t)lev;

                        if (cx == RC_SAMPLES_NB-1) { *ce = 0; *as = ADSR_REL;}
                        break;

                    case ADSR_REL:            
                        // valeurs 1-x
                        cx=adsrNext(ce,adsrDurRel[a]);
                        ov0=rcCurve[cx];
                        ov_=(uint16_t)(lev-(lev*ov0)/(uint32_t)P16);

                        if (cx == RC_SAMPLES_NB-1) { *ce = 0; *as = ADSR_OFF;ov_=0;} 
                        break;

                    default:break;
                }
                adsrLastOut[a]=ov_;     // kept for the next trigger
                ov_ >>= 1;      // single conversion from the internal ~0-65536 scale to the int16 scale used by update_inputs, done once for all four stages
                
                // update connected level inputs
                int16_t in_lev_id=ctl_output_id_chain[adsr_ctl_output_id[a][ADSR_SHAPE]];
                if (__builtin_expect(in_lev_id != NO_LINK, 0)){
                    ov_ &= 0x7fff; // écrêtage
                    update_inputs(in_lev_id,ov_);
                }

                adsrScopeBufReal[a*ADSR_SCOPE_BUFFER_LEN + *ap]=ov_;

                (*ap)++;  // until ADSR_OFF
            }
            else if(__builtin_expect(*ap!=0 && *ap<(ADSR_SCOPE_BUFFER_LEN-1),0)){       // effacement fin de courbe
                int32_t* ab=&adsrScopeBufReal[a*ADSR_SCOPE_BUFFER_LEN];
                for(uint16_t x=*ap;x<ADSR_SCOPE_BUFFER_LEN;x++){*(ab+x)=0;}
                *ap=0;adsrScopeDisp[a]=true;   
            }
        
            if(__builtin_expect(*ap>=ADSR_SCOPE_BUFFER_LEN,0)){*ap=0;adsrScopeDisp[a]=true;}
        
            // update connected bool inputs
            int16_t in_bool_id=ctl_output_id_chain[adsr_ctl_output_id[a][ADSR_GATE]];
            if (__builtin_expect(in_bool_id != NO_LINK, 0)){
                update_inputs(in_bool_id,bol);
            }
        }
//gpio_put(TST_PIN,0);restore_interrupts(ints);        
    }
}

void irq_button_init(uint8_t pin)
{
    gpio_init(pin);gpio_set_function(pin, GPIO_FUNC_SIO);gpio_set_dir(pin,GPIO_IN);
    gpio_init(BUT_VCC_PIN);gpio_set_function(BUT_VCC_PIN, GPIO_FUNC_SIO);gpio_set_dir(BUT_VCC_PIN,GPIO_OUT);
    gpio_put(BUT_VCC_PIN,LOW);sleep_ms(100);gpio_put(BUT_VCC_PIN,HIGH);
    gpio_irq_init(pin);  // après  init_global_dma_irq();
}

void touch_button_init(uint8_t pin)
{
    gpio_init(pin);gpio_set_function(pin, GPIO_FUNC_SIO);gpio_set_dir(pin,GPIO_IN);
}

void __not_in_flash_func(touch_button_handler)(uint8_t touchButtonNb,bool* touchButtonValue,volatile bool* coderTouchB)
{
            *touchButtonValue=!(*touchButtonValue);
            int16_t in_bool_id=ctl_output_id_chain[tbut_ctl_output_id[touchButtonNb][0]];
            //printf("c:%u csw:%u id:%i\n",touchButtonNb,*touchButtonValue,in_bool_id);
            
            if (__builtin_expect(in_bool_id != NO_LINK, 0)){     
                update_inputs(in_bool_id,*touchButtonValue *2-1);
            }
        
            if(coderTouchB!=nullptr){
                coderTouchB[touchButtonNb]=*touchButtonValue;
            }
}

/* ******  lf mixers  ****** */
//
// every change of a coder goes through setLfmCoders ; every change of an input value goes through setLfm (called by update_inputs)
// 
//
// current features : input 0 gen level ; 1,2,3 mixed inputs 
//                  every input has 1 level coder (captured as 0-255 ; extended to 16 bits)
//                  every input has 1 att coder (captured as 0-255 ; stored unchanged)
//                  all inputs are int16_t                 
//                  every input with a type : 0 lin , 1 log , 2 inv log (code for log to be added)
// log mode to be discussed (currently lin only - use 2 tables to convert lin to log or ilog ; where should the conversion be done ? )
// 
// 
//

void lf_mixer_init()
{
    printf("%u lfm init\n",MAX_LFM);
    for(uint8_t lfm=0;lfm<MAX_LFM;lfm++){
        for(uint8_t inp=0;inp<LFM_INPUTS_NB;inp++){   
            lfmCoder[inp][lfm]=0;
            lfmCoderAtt[inp][lfm]=0;
            lfmInputType[inp][lfm]=0;
        }
        intermediateOutputValues[lfm]=0;
        lfmGenAttValue[lfm]=0;
        lfmOutputValues[lfm]=0;
        lfmScopeBufPtr[lfm]=0;
    }
}

static_assert(LFM_INPUTS_NB>=4,"setLfm needs 4 inputs (0 to 3)");

static volatile uint8_t lfmDepth=0;
// compute only: from the stored coders and the stored values of the inputs
void __not_in_flash_func(setLfm)(uint8_t lfm)
{
    if(++lfmDepth>4){lfmDepth=0;printf("setLfm recursion lfm:%u\n",lfm);return;}

    // ---- general gain: base coder + attenuated input 0 ----
    lfmGenAttValue[lfm] =                                                   // lfmGen int32 ; lfmCoder uint16 ; input value int16 ; lfmCoderAtt uint16
        (lfmCoder[0][lfm]<<CODER_BASE_SHIFT) +
        ((ctl_input_val[lfm_ctl_input_id[0][lfm]] * lfmCoderAtt[0][lfm]) >> MAX_CTL_ATT_SHIFT);

    // ---- base coders sum of the inputs 1-3 ----
    // (worst case 3 * 0xff << 7 = 0x017f80)
    int32_t v0 = (lfmCoder[1][lfm] + lfmCoder[2][lfm] + lfmCoder[3][lfm])
                 << (CODER_BASE_SHIFT);

    // ---- attenuated inputs sum ----
    // (worst case: 3 * 0x7fff * 0xff = 0x017ffffd, it fits in 32 bits)
    int32_t v1 = ((int32_t)ctl_input_val[lfm_ctl_input_id[1][lfm]] * lfmCoderAtt[1][lfm]
                + (int32_t)ctl_input_val[lfm_ctl_input_id[2][lfm]] * lfmCoderAtt[2][lfm]
                + (int32_t)ctl_input_val[lfm_ctl_input_id[3][lfm]] * lfmCoderAtt[3][lfm]) >> MAX_CTL_ATT_SHIFT;

    // ---- intermediate value, clipped to 16 bits ----
    int32_t iv = v0 + v1;
    if (iv > INT16_MAX) iv = INT16_MAX;
    if (iv < INT16_MIN) iv = INT16_MIN;
    intermediateOutputValues[lfm] = (int16_t)iv;

    // ---- apply the general gain ----
    iv = (iv * lfmGenAttValue[lfm]) >> 15;

    // ---- high ovf (+32767) ----
    uint32_t carry_hi = (iv <= 0x7FFF);
    iv = (iv & -carry_hi) | (0x7FFF & ~(-carry_hi));

    // ---- low ovf (-32768) ----
    uint32_t carry_lo = (iv >= -0x8000);
    iv = (iv & -carry_lo) | (-0x8000 & ~(-carry_lo));

    lfmOutputValues[lfm] = (int16_t)iv;

    update_inputs(ctl_output_id_chain[lfm_ctl_output_id[0][lfm]],lfmOutputValues[lfm]);
    
    lfmDepth--;
}

// load the coders of an input (0 = general gain, 1-3 = mixed inputs), then compute the output
void __not_in_flash_func(setLfmCoders)(uint8_t lfm,uint8_t inp,uint16_t coder,uint16_t coderAtt)
{
    const uint32_t irq=save_and_disable_interrupts();                       // the interrupt that updates the inputs computes the same output: no interleaving
    lfmCoder[inp][lfm]    = coder;
    lfmCoderAtt[inp][lfm] = coderAtt;
    setLfm(lfm);
    restore_interrupts(irq);
}

void __not_in_flash_func(lfmScopeHandler)()
{
    if((millisCounter-lfmTime)>lfmScopeTimingInterval && setupComplete){
        lfmTime=millisCounter;
        for(uint8_t l=0;l<MAX_LFM;l++){
            uint16_t* lp=&lfmScopeBufPtr[l];
            lfmScopeBufReal[l*LFM_SCOPE_BUFFER_LEN+*lp]=lfmOutputValues[l];
            //printf(" %l",lfmOutputValues[l]);
            (*lp)++;
            if(__builtin_expect(*lp>=LFM_SCOPE_BUFFER_LEN,0)){*lp=0;lfmScopeDisp[l]=true;}
        }
        //uint8_t l=1;printf("l:%u %i\n",l,lfmOutputValues[l]);
    }
    //printf("\n");
}

void disp_lfm(uint8_t lfm,char* t)
{
    if(t!=nullptr){printf("%s",t);}
    uint8_t maxLfm=lfm;
    uint8_t minLfm=lfm;
    if(lfm>MAX_LFM){
        maxLfm=MAX_LFM-1;
        minLfm=0;
    } 

    printf("\nlf_mux   c0   ca0  (id)    c1   ca1  (id)    c2   ca2  (id)   iov (id)\n");
    for(uint8_t i=minLfm;i<=maxLfm;i++){
        printf("m%i:  ",i);
        for(uint8_t j=0;j<LFM_INPUTS_NB;j++){
            printf(" %5i",lfmVar[j*2][i]);
            printf(" %5i(%4i)",lfmVar[j*2+1][i],lfm_ctl_input_id[j][i]);
        }
        printf(" %-5i %i\n",intermediateOutputValues[i],lfm_ctl_output_id[0][i]);
    }
    if(lfm>MAX_LFM){printf("\n");}
}

void disp_lfm(uint8_t lfm){
    disp_lfm(lfm,nullptr);
}

/* ****** reverb/echo ****** */

// init of the effects: coders at 0, delay lines and states cleared, echo values computed
// to call after the tables (fillDur, fillAmplIncr) and the inputs (objects_table_init): the input ids are not touched here
void effects_init()
{
    for (uint8_t r = 0; r < MAX_REV; r++) {
        revDurCoder[r]    = 0;
        revShiCoder[r]    = 0;
        revRetCoder[r]    = 0;
        revDurCoderAtt[r] = 0;
        revShiCoderAtt[r] = 0;
        revRetCoderAtt[r] = 0;
    }
    memset(bufReverb, 0x00, sizeof(bufReverb));

    if (echoDmaChan < 0) { echoDmaChan = dma_claim_unused_channel(true); }
    for (uint8_t e = 0; e < MAX_ECHO; e++) {
        echoDurCoder[e]    = 0;
        echoInjCoder[e]    = 0;
        echoRetCoder[e]    = 0;
        echoDurCoderAtt[e] = 0;
        echoInjCoderAtt[e] = 0;
        echoRetCoderAtt[e] = 0;
        memset(&echoSt[e], 0, sizeof(EchoState));
        echoSt[e].state   = ECHO_IDLE;                          // bufEcho is cleared below
        echoSt[e].fadePos = ECHO_FADE_LEN;
        echoBusUsed[e]    = false;
        setEcho(e);
    }
    memset(bufEcho, 0x00, sizeof(bufEcho));
    memset(echoBus, 0x00, sizeof(echoBus));

    for (uint8_t v = 0; v < MAX_VOICES; v++) { setVoiceEcho(&voices[v]); }
    printf("effects_init: %u echo, delay %ld steps (%ld ms), dma %d\n", (unsigned)MAX_ECHO, (long)echoDelay[0], (long)(echoDelay[0] * 1000 / 22050), echoDmaChan);
}

static inline int32_t sat32(int64_t x)
{
    return (x > INT32_MAX) ? INT32_MAX : ((x < INT32_MIN) ? INT32_MIN : (int32_t)x);
}

// level of a send or return: table value of the coder + attenuated input, as the wave levels of setVoiceAmpl
static inline int32_t echoLevel(int32_t coder, int32_t att, int16_t input)
{
    if (coder < 0) coder = 0;
    if (coder > WAVE_LEVEL_STEPS - 1) coder = WAVE_LEVEL_STEPS - 1;
    int32_t v = (int32_t)waveLevel[coder] + (((int32_t)input * att) >> MAX_CTL_ATT_SHIFT);
    if (v < 0) v = 0;
    if (v > 32767) v = 32767;
    return v;
}

// compute: delay, feedback and return of an echo, from the stored coders and the stored values of its inputs
void __not_in_flash_func(setEcho)(uint8_t echo)
{
    if (echo >= MAX_ECHO) return;

    const int32_t n = ctlCombine(echoDurCoder[echo], echoDurCoderAtt[echo], ctl_input_val[echo_ctl_input_id[ECHD][echo]], ECHO_DUR_SHIFT, 0, DUR_ECH_NB - 1);
    int32_t d = ECHO_MIN_DELAY + (int32_t)(((uint32_t)yTableQ16[n] * (uint32_t)(ECHO_BUF_LEN - ECHO_MIN_DELAY)) / yTableQ16[DUR_ECH_NB - 1]);
    if (d > ECHO_BUF_LEN - 1) d = ECHO_BUF_LEN - 1;             // the read index must never be the write index
    echoDelay[echo] = d;

    const int32_t i = ctlCombine(echoInjCoder[echo], echoInjCoderAtt[echo], ctl_input_val[echo_ctl_input_id[ECHI][echo]], ECHO_INJ_SHIFT, 0, ECHO_INJ_STEPS - 1);
    echoFb[echo] = (i * ECHO_FB_MAX_Q15) / (ECHO_INJ_STEPS - 1);

    echoRet[echo] = echoLevel(echoRetCoder[echo], echoRetCoderAtt[echo], ctl_input_val[echo_ctl_input_id[ECHR][echo]]);
}

// load the coders of an echo, then compute it (same model as setVoiceAmplCoders)
void __not_in_flash_func(setEchoCoder)(uint8_t echo, uint16_t coderDur, uint16_t coderInj, uint16_t coderRet, uint16_t coderDurA, uint16_t coderInjA, uint16_t coderRetA)
{
    if (echo >= MAX_ECHO) return;
    const uint32_t irq = save_and_disable_interrupts();         // the interrupt that updates the inputs computes the same values: no interleaving
    echoDurCoder[echo]    = coderDur;
    echoInjCoder[echo]    = coderInj;
    echoRetCoder[echo]    = coderRet;
    echoDurCoderAtt[echo] = coderDurA;
    echoInjCoderAtt[echo] = coderInjA;
    echoRetCoderAtt[echo] = coderRetA;
    setEcho(echo);
    restore_interrupts(irq);
}

// compute: the echo send level of a voice, from its stored coders and the stored value of its input
void __not_in_flash_func(setVoiceEcho)(Voice* v)
{
    v->echoAtt = (int16_t)echoLevel(v->coderEcho, v->coderEchoAtt, ctl_input_val[v->voice_ctl_input_id[VECH]]);
}

// load the echo send coders of a voice, then compute its send level
void __not_in_flash_func(setVoiceEchoCoders)(Voice* v, uint16_t coder, uint16_t coderAtt)
{
    const uint32_t irq = save_and_disable_interrupts();
    v->coderEcho    = (int16_t)coder;
    v->coderEchoAtt = (int16_t)coderAtt;
    setVoiceEcho(v);
    restore_interrupts(irq);
}

// clear of bufEcho by DMA, the CPU does not wait: returns true once bufEcho is clean (the echo is then ECHO_IDLE)
static bool __not_in_flash_func(echoClearService)(uint8_t echo)
{
    EchoState& s = echoSt[echo];
    if (!s.clearStarted) {
        if (dma_channel_is_busy(echoDmaChan)) return false;     // channel still clearing another echo: retry at the next buffer
        dma_channel_config c = dma_channel_get_default_config(echoDmaChan);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, false);           // always the same zero word
        channel_config_set_write_increment(&c, true);
        dma_channel_configure(echoDmaChan, &c, bufEcho[echo], &echoZero, (ECHO_BUF_LEN * 2) / 4, true);
        s.clearStarted = true;
        return false;
    }
    if (dma_channel_is_busy(echoDmaChan)) return false;
    s.state = ECHO_IDLE;
    return true;
}

// echo of every echo object, added to the audio buffer (left = right); nothing is computed for an echo that is idle and receives nothing
void __not_in_flash_func(echoProcess)(int32_t* vBuffer)
{
    for (uint8_t e = 0; e < MAX_ECHO; e++) {
        EchoState& s = echoSt[e];
        const bool used = echoBusUsed[e];
        echoBusUsed[e] = false;

        if (s.state == ECHO_CLEARING) {
            if (!used) { echoClearService(e); continue; }
            while (!echoClearService(e)) {}                     // a voice sends again before the end of the clear: wait (tens of us, rare)
        }
        if (s.state == ECHO_IDLE) {
            if (!used) continue;
            s.state   = ECHO_RUNNING;                           // start: bufEcho is clean, the settings are taken as they are
            s.silent  = 0;
            s.yLast   = 0;
            s.fbLast  = echoFb[e];
            s.retLast = echoRet[e];
            s.dCur    = echoDelay[e];
            s.dOld    = s.dCur;
            s.fadePos = ECHO_FADE_LEN;
        }

        // a new delay starts a crossfade, only once the previous one is over: a delay that keeps moving is followed by steps of 46 ms
        const int32_t dTarget = echoDelay[e];
        if (s.fadePos >= ECHO_FADE_LEN && dTarget != s.dCur) {
            s.dOld    = s.dCur;
            s.dCur    = dTarget;
            s.fadePos = 0;
        }

        // feedback and return ramp over the buffer, from their value at the end of the previous buffer
        const int32_t fbEnd  = echoFb[e];
        const int32_t retEnd = echoRet[e];
        int32_t       fbAcc  = s.fbLast << ECHO_STEPS_SHIFT;
        const int32_t fbInc  = fbEnd - s.fbLast;
        int32_t       retAcc = s.retLast << ECHO_STEPS_SHIFT;
        const int32_t retInc = retEnd - s.retLast;

        int16_t*      buf     = bufEcho[e];
        int32_t*      bus     = echoBus[e];
        int32_t       w       = s.w;
        const int32_t dCur    = s.dCur;
        const int32_t dOld    = s.dOld;
        int32_t       fadePos = s.fadePos;
        int32_t       yPrev   = s.yLast;
        int32_t       peak    = 0;
        int32_t*      vb      = vBuffer;

        for (uint32_t k = 0; k < ECHO_STEPS; k++) {
            int32_t r = w - dCur;
            if (r < 0) r += ECHO_BUF_LEN;
            int32_t y = buf[r];                                 // the input of dCur steps ago
            if (fadePos < ECHO_FADE_LEN) {
                int32_t ro = w - dOld;
                if (ro < 0) ro += ECHO_BUF_LEN;
                const int32_t yo = buf[ro];
                y = yo + (((y - yo) * fadePos) >> ECHO_FADE_SHIFT);    // linear crossfade from the old delay to the new one
                fadePos++;
            }

            fbAcc  += fbInc;
            retAcc += retInc;

            int32_t x = (bus[k] >> 16) + ((y * (fbAcc >> ECHO_STEPS_SHIFT)) >> 15);   // input + feedback ; a full-scale voice gives 16384: 6 dB of headroom for the feedback
            if (x >  32767) x =  32767;
            if (x < -32768) x = -32768;
            buf[w] = (int16_t)x;

            const int32_t ret = retAcc >> ECHO_STEPS_SHIFT;
            const int32_t o0  = sat32(((int64_t)(yPrev + y) * ret) << 1);   // back to 44100 Hz: the mean of two steps, then the step itself
            const int32_t o1  = sat32(((int64_t)y * ret) << 2);             // 16384 * 32767 * 4 = full scale of a voice
            vb[0] += o0;
            vb[1] += o0;
            vb[2] += o1;
            vb[3] += o1;
            vb += 4;

            const int32_t ay = (y < 0) ? -y : y;
            if (ay > peak) peak = ay;
            yPrev = y;
            if (++w >= ECHO_BUF_LEN) w = 0;
        }

        s.w       = w;
        s.fadePos = fadePos;
        s.yLast   = yPrev;
        s.fbLast  = fbEnd;
        s.retLast = retEnd;

        if (used) {
            memset(bus, 0, sizeof(echoBus[0]));                 // ready for the next buffer
            s.silent = 0;
        }
        else {
            // stop once the output has stayed below ECHO_SILENCE during a whole delay: what is left in bufEcho is then below it too
            s.silent = (peak < ECHO_SILENCE) ? (s.silent + ECHO_STEPS) : 0;
            const int32_t span = ((dCur > dOld) ? dCur : dOld) + ECHO_STEPS;
            if (s.silent >= span && fadePos >= ECHO_FADE_LEN) {
                s.state        = ECHO_CLEARING;                 // clear bufEcho, otherwise its residue would come back when a voice sends again
                s.clearStarted = false;
                echoClearService(e);
            }
        }
    }
}

