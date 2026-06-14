#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "const.h"
#include "miscControls.h"
#include "input_tables_management.h"

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
uint8_t  adsrStatus[MAX_ADSR];                          // voir AdsrStates
uint32_t adsrCurrEch[MAX_ADSR];
uint32_t adsrCurrEchFra[MAX_ADSR];
uint16_t adsrStepInt[MAX_ADSR][ADSR_MAX_STATES];
uint16_t adsrStepFra[MAX_ADSR][ADSR_MAX_STATES];
int16_t  adsrOutputsValues[MAX_ADSR];
int16_t  adsr_ctl_input_id[MAX_ADSR];
int16_t  adsr_ctl_output_id[MAX_ADSR];
int32_t  adsrScopeBufReal[MAX_ADSR*ADSR_SCOPE_BUFFER_LEN];  // real values
uint16_t adsrScopeBufPtr[MAX_ADSR];

uint32_t    adsrTime=0;
uint32_t    adsrTimingInterval=1000/ADSR_SAMPLE_RATE;

extern uint32_t millisCounter;
extern uint16_t amplLevel[];
extern int16_t  ctl_output_id_chain[];



uint32_t durTable[DUR_ECH_NB];


void adsrInit()
{
    for(uint8_t a=0;a<MAX_ADSR;a++){
        adsrCoderAtt[a]=0;setAdsrDur(a,ADSR_ATT,adsrCoderAtt[a]);
        adsrCoderDec[a]=127;setAdsrDur(a,ADSR_DEC,adsrCoderDec[a]);
        adsrCoderSus[a]=10;setAdsrDur(a,ADSR_SUS,adsrCoderSus[a]);
        adsrCoderRel[a]=100;setAdsrDur(a,ADSR_REL,adsrCoderRel[a]);
        adsrCoderLev[a]=ADSR_MAX_LEVEL_CODERS/2;setAdsrLev(a,adsrCoderLev[a]);
        adsrStatus[a]=ADSR_OFF;
        adsrScopeBufPtr[a]=0;

        adsrCoderAttAtt[a]=FULL_ATTENUATION_VALUE;
        adsrCoderDecAtt[a]=FULL_ATTENUATION_VALUE;
        adsrCoderSusAtt[a]=FULL_ATTENUATION_VALUE;
        adsrCoderRelAtt[a]=FULL_ATTENUATION_VALUE;
        adsrCoderLevAtt[a]=FULL_ATTENUATION_VALUE;
    }
}


#define DUR_MAX        128        // nombre de valeurs de durée
#define SAMPLES_NB     256        // taille de la courbe RC
#define GAMMA          2.5f       // creusité de la courbe (≈2% en haut pour DUR_MAX=128)
#define T_MIN_TICKS    1.0f       // durée minimale (en ticks ADSR)
#define T_MAX_TICKS    5000.0f    // durée maximale (en ticks ADSR)

// Tables générées
float    sens[DUR_MAX];
float    T_ticks[DUR_MAX];
uint32_t step_q16[DUR_MAX];
uint16_t rc_curve[SAMPLES_NB];

// Approximation rapide de log2(x)
static inline float fast_log2(float x)
{
    union { float f; uint32_t i; } vx = { x };
    float y = (float)vx.i;
    y *= 1.0f / (1 << 23);

    float e = y - 127.0f;          // exposant
    float m = (vx.i & 0x7FFFFF) / (float)(1 << 23); // mantisse normalisée

    // approx log2(1+m)
    float log2m = m * (1.3465558f + m * (-0.3606741f + m * 0.0454377f));

    return e + log2m;
}

// Approximation rapide de exp2(x)
static inline float fast_exp2(float x)
{
    int ipart = (int)x;
    float fpart = x - ipart;

    // approx 2^fpart
    float poly = 1.0f + fpart * (0.69314718f +
                 fpart * (0.24022651f +
                 fpart * (0.05550411f)));

    uint32_t i = (ipart + 127) << 23;

    union { uint32_t i; float f; } vx = { i };
    return vx.f * poly;
}


// Puissance générique : x^y
float pow_approx(float x, float y)
{
    if (x <= 0.0f) {
        return 0.0f; // pour ton usage ADSR, 0^y = 0 est ce qu’on veut
    }

    float logx = fast_log2(x);
    float e = y * logx;
    return fast_exp2(e);
}

void fillDur(void)
{
    // --- 1) Table de sensibilité : s = (dur/DUR_MAX)^gamma ---
    for (int dur = 0; dur < DUR_MAX; dur++) {
        float x = (float)dur / (float)(DUR_MAX - 1);
        if (dur == 0) {
            sens[dur] = 0.0f;
        } else {
            sens[dur] = pow_approx(x, GAMMA);
        }
    }

    // --- 2) Durée en ticks : interpolation entre T_min et T_max ---
    for (int dur = 0; dur < DUR_MAX; dur++) {
        T_ticks[dur] = T_MIN_TICKS + sens[dur] * (T_MAX_TICKS - T_MIN_TICKS);
    }

    // --- 3) Table de step Q16.16 ---
    for (int dur = 0; dur < DUR_MAX; dur++) {
        float step = (float)SAMPLES_NB / T_ticks[dur];
        step_q16[dur] = (uint32_t)(step * 65536.0f);
    }

    // --- 4) Table RC : y[i] = 1 - exp(-i/tau) ---
    // Choix de tau pour atteindre ~99% à la fin
    float target = 0.99f;
    float ln = fast_log2(1.0f - target) * 0.69314718056f;
    float tau = -(float)(SAMPLES_NB - 1) / ln;

    for (int i = 0; i < SAMPLES_NB; i++) {
        float y = 1.0f - fast_exp2(-(float)i / (tau * 1.44269504089f));
        // car exp(x) = 2^( x / ln(2) )
        rc_curve[i] = (uint16_t)(y * 65535.0f);   // Q0.16 ou Q1.15 selon ton moteur
    }

    // --- Affichage pour vérification ---
    for (int dur = 0; dur < DUR_MAX; dur++) {
        printf("%3d  sens=%f  T=%f  step_q16=%u\n",
               dur, sens[dur], T_ticks[dur], step_q16[dur]);
    }
}

/*static inline uint32_t exp_neg_q16(uint32_t zQ16)
{
    // zQ16 : Q16
    uint64_t z2 = (uint64_t)zQ16 * zQ16 >> 16;      // z^2 en Q16
    uint64_t denom = (1u << 16) + zQ16 + (z2 >> 1); // 1 + z + z^2/2

    // retourne ~exp(-z) en Q16
    return (uint32_t)(((uint64_t)1 << 32) / denom);
}


void fillDur(void)
{
    const uint32_t stepMaxQ16 = 512u << 16;      // 512.0
    const uint32_t stepMinQ16 = (1u << 16) >> 1; // 0.5

    const uint32_t maxIndex = DUR_ECH_NB - 1;

    // k ~ 3.0 pour une forme RC réaliste
    const uint32_t kQ16 = 3u << 16; // k = 3

    // fEnd = exp(-k * 1) en Q16
    uint32_t fEndQ16 = exp_neg_q16(kQ16);
    uint32_t rangeFQ16 = (1u << 16) - fEndQ16; // 1 - fEnd

    for (uint32_t i = 0; i < DUR_ECH_NB; i++)
    {
        // u = i / maxIndex en Q16
        uint32_t uQ16 = ((uint64_t)i << 16) / maxIndex;

        // z = k * u
        uint32_t zQ16 = (uint64_t)kQ16 * uQ16 >> 16;

        // f = exp(-k*u) en Q16
        uint32_t fQ16 = exp_neg_q16(zQ16);

        // normalisation pour respecter exactement les bornes
        // fNorm = (f - fEnd) / (1 - fEnd)
        uint32_t numF = fQ16 - fEndQ16;

        // step = stepMin + (stepMax - stepMin) * fNorm
        uint32_t stepQ16 =
            stepMinQ16 +
            (uint64_t)(stepMaxQ16 - stepMinQ16) * numF / rangeFQ16;

        durTable[i] = stepQ16;
    }
}*/


/*static inline uint32_t q16_sqrt(uint32_t x)
{
    // sqrt Q16 → Q16 (méthode de Newton rapide)
    uint32_t r = x;
    for (int i = 0; i < 4; i++)
        r = (r + ((uint64_t)x << 16) / r) >> 1;
    return r;
}

void fillDur(void)  // f=x^(3/2)
{
    const uint32_t stepMaxQ16 = (BASIC_WAVE_TABLE_LEN/4) << 16;      // 512.0
    const uint32_t stepMinQ16 = (1 << 16) >> 1; // 0.5

    uint32_t maxIndex = DUR_ECH_NB - 1;

    for (int i = 0; i < DUR_ECH_NB; i++)
    {
        // x = (127 - i) / 127  en Q16
        uint32_t xQ16 = ((uint64_t)(maxIndex) - i) << 16) / maxIndex;

        // f = x * sqrt(x)
        uint32_t sqrtX = q16_sqrt(xQ16);
        uint32_t fQ16 = (uint64_t)xQ16 * sqrtX >> 16;

        // step = stepMin + (stepMax - stepMin) * f
        uint32_t stepQ16 =
            stepMinQ16 +
            (((uint64_t)(stepMaxQ16 - stepMinQ16) * fQ16) >> 16);

        durTable[i] = stepQ16;
    }
}*/

/*void fillDur(void)    // exp
{
    const uint32_t stepMaxQ16 = (BASIC_WAVE_TABLE_LEN/4) << 16;      // 512.0
    const uint32_t stepMinQ16 = (1 << 16) >> 1; // 0.5

    uint32_t maxIndex = DUR_ECH_NB - 1;

    for (int i = 0; i < DUR_ECH_NB; i++)
    {
        // x = (127 - i) / 127  en Q16
        uint32_t xQ16 = ((uint64_t)(maxIndex - i) << 16) / maxIndex;

        // f = x^2  (toujours en Q16)
        uint32_t fQ16 = (uint64_t)xQ16 * xQ16 >> 16;

        // step = stepMin + (stepMax - stepMin) * f
        uint32_t stepQ16 =
            stepMinQ16 +
            (((uint64_t)(stepMaxQ16 - stepMinQ16) * fQ16) >> 16);

        durTable[i] = stepQ16;
    }
}*/

void __not_in_flash_func(setAdsrLev)(uint8_t adsr,int32_t val)
{
    adsrCoderLev[adsr]=val;
}

void __not_in_flash_func(setAdsrDur)(uint8_t adsr,uint8_t adsrStatus,int32_t val)
{

    // Q16
/*
    uint32_t stepQ16 = durTable[val];

    adsrStepInt[adsr][adsrStatus]=stepQ16 >> 16;
    adsrStepFra[adsr][adsrStatus]=stepQ16 & 0xFFFF;
    printf("a:%d as:%d v:%d si:%d sf:%d\n",adsr,adsrStatus,val,adsrStepInt[adsr][adsrStatus],adsrStepFra[adsr][adsrStatus]);
*/
}

/*void __not_in_flash_func(adsrEchTime)(uint8_t adsr_nb,uint8_t* adsrStatus,uint32_t* ce,uint32_t* cf)
{
    if(*ce >= ((BASIC_WAVE_TABLE_LEN / 4)-1)){
        *ce=0;*cf=0;(*adsrStatus)++;   // nouveau status
        if(*adsrStatus>ADSR_REL){*adsrStatus=ADSR_OFF;}
    }
    else {
        *cf+=adsrStepFra[adsr_nb][*adsrStatus];
        uint32_t carry = (*cf >= MAX_STEP_FRA);
        *cf -= carry * MAX_STEP_FRA;
        *ce += adsrStepInt[adsr_nb][*adsrStatus] + carry;
        if(*ce >= ((BASIC_WAVE_TABLE_LEN / 4)-1)){*ce=(BASIC_WAVE_TABLE_LEN / 4);}
    }        
}*/

// acc_q16 : accumulateur Q16.16 (à conserver entre appels)
// dur     : index 0..DUR_MAX-1
// retourne un échantillon 0..65535
static inline uint16_t __not_in_flash_func(adsr_next)(uint32_t *acc_q16, int dur)
{
    // avance dans la courbe
    *acc_q16 += step_q16[dur];

    // index dans la table RC
    uint32_t idx = *acc_q16 >> 16;
    if (idx >= SAMPLES_NB)
        idx = SAMPLES_NB - 1;

    return rc_curve[idx];
}

void __not_in_flash_func(adsrHandler)()
{
    if((millisCounter-adsrTime)>adsrTimingInterval){
        adsrTime=millisCounter;

        for(uint8_t a=0;a<MAX_ADSR;a++)
        {
            int16_t*  ov=&adsrOutputsValues[a];
            int16_t   ov0;
            uint8_t*  as=&adsrStatus[a];
            uint16_t* ap=&adsrScopeBufPtr[a];
            if (__builtin_expect(*as != ADSR_OFF, 0)) {

                uint8_t   out_id;
                uint16_t  lev=amplLevel[adsrCoderLev[a]];
                uint32_t  cx=0;
                uint32_t* ce=&adsrCurrEch[a];              
                #define P15 (1<<15)         
      

                //adsrEchTime(a,as,ce,&adsrCurrEchFra[a]);

                switch(*as){
                    case ADSR_ATT:
                        *ov = adsr_next(ce, adsrCoderAtt[a]);
                        if (*ov == rc_curve[SAMPLES_NB - 1]) { *ce = 0; *as = ADSR_DEC;}; 
                        //*ov=rc_tables[32][*ce][LSIN]; 
                        break;
                    case ADSR_DEC:
                        // valeurs 1-x
                        ov0 = adsr_next(ce, adsrCoderDec[a]);
                        *ov  = P15 - (P15 - lev) * ov0 / P15;
                        if (ov0 == rc_curve[SAMPLES_NB - 1]) { *ce = 0; *as = ADSR_SUS; }
                        //cx=(rc_tables[32][*ce][LSIN]);
                        //*ov=P15-(P15-lev)*cx/P15;
                        break;
                    case ADSR_SUS:
                        // on utilise amplLevel[adsrLevCoder]
                        ov0 = adsr_next(ce, adsrCoderSus[a]);   // timing management
                        if (ov0 == rc_curve[SAMPLES_NB - 1]) { *ce = 0; *as = ADSR_DEC; }
                        *ov=lev;
                        break;
                    case ADSR_REL:            
                        // valeurs 1-x
                        ov0 = adsr_next(ce, adsrCoderRel[a]);
                        *ov  = lev - lev * ov0 / P15;
                        if (ov0 == rc_curve[SAMPLES_NB - 1]) { *ce = 0; *as = ADSR_OFF; }
                        //cx=(rc_tables[32][*ce][LSIN]);
                        //*ov=lev-(cx*lev/P15);
                        break;
                    default:break;
                }
                
                printf("adsr:%d as:%d ov:%d ce:%u sti:%u stf:%u ptr:%u\n",a,*as,*ov,*ce,adsrStepInt[a][*as],adsrStepFra[a][*as],*ap);       
                out_id=ctl_output_id_chain[adsr_ctl_output_id[a]];
                if (__builtin_expect(out_id != NO_LINK, 0)){update_inputs(out_id,*ov);}

                adsrScopeBufReal[a*ADSR_SCOPE_BUFFER_LEN + *ap]=*ov;
                adsrScopeBufPtr[a]++;if(__builtin_expect(*ap>ADSR_SCOPE_BUFFER_LEN,0)){*ap=0;}
            }
            else if(__builtin_expect(*ap!=0 && *ap<ADSR_SCOPE_BUFFER_LEN,0)){
                int32_t* ab=&adsrScopeBufReal[a*ADSR_SCOPE_BUFFER_LEN];
                for(uint16_t x=*ap;x<ADSR_SCOPE_BUFFER_LEN;x++){*(ab+x)=0;}
                *ap=0;
            }

        }
    }
}