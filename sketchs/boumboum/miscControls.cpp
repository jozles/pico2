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
uint16_t adsrOutputsValues[MAX_ADSR];
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


#define DUR_MAX     DUR_ECH_NB
#define SAMPLES_NB  256

uint32_t stepTableQ16[DUR_MAX];     // coefficient RC par durée (Q16)
uint16_t rcCurve[SAMPLES_NB]; // courbe RC 0..65535

// Paramètres modifiables
uint32_t gammaQ16 = 32768;   // ≈0.6  courbure durée (0.3–2.0)
uint32_t betaQ16  = 4096;   // ≈0.5  courbure RC (0.3–2.0)

uint32_t stepMinQ16 = 32768;                 // 0.5 en Q16
uint32_t stepMaxQ16 = (SAMPLES_NB << 16);    // SAMPLES_NB en Q16

void fillDur(void)
{
    const uint32_t ONE_Q16 = 65536;
    // 1) stepTable : dur=0 -> SAMPLES_NB, dur=DUR_MAX -> 0.5
    for (uint32_t d = 0; d < DUR_MAX; d++)
    {
        // x = 0..1 en Q16
        uint32_t xQ16 = (d * 65535u) / (DUR_MAX - 1);

        // courbe non linéaire simple : y = x²
        uint32_t yQ16 = (uint32_t)(((uint64_t)xQ16 * xQ16) >> 16);

        // stepMax = SAMPLES_NB
        uint32_t stepMaxQ16 = (SAMPLES_NB << 16);

        // stepMin = 0.5
        uint32_t stepMinQ16 = ONE_Q16 >> 1;

        uint32_t delta = stepMaxQ16 - stepMinQ16;

        // interpolation non linéaire
        uint64_t prod = (uint64_t)delta * (uint64_t)yQ16;
        stepTableQ16[d] = stepMaxQ16 - (uint32_t)(prod >> 16);
        printf("%u %u\n",d,stepTableQ16[d]);
    }

    // 2) rcCurve : approximation tension condensateur RC
    uint32_t yQ16 = 0;
    //uint32_t kQ16 = (uint32_t)(229900u / SAMPLES_NB);       // 229900u equiv 97% du sommet de la courbe au dernier step
    //uint32_t kQ16 = (uint32_t)(196608u / SAMPLES_NB);       // 229900u equiv 95% du sommet de la courbe au dernier step
    //uint32_t kQ16 = (uint32_t)(174240u / SAMPLES_NB);       // 229900u equiv 93% du sommet de la courbe au dernier step    
    uint32_t kQ16 = (uint32_t)(157696u / SAMPLES_NB);       // 229900u equiv 91% du sommet de la courbe au dernier step  

    for (int i = 0; i < SAMPLES_NB; i++)
    {
        uint32_t diff = ONE_Q16 - yQ16;
        uint32_t dy   = (uint32_t)(((uint64_t)diff * kQ16) >> 16);
        yQ16 += dy;
        if (yQ16 > ONE_Q16) yQ16 = ONE_Q16;

        rcCurve[i] = (uint16_t)((yQ16 * 65535ULL) >> 16);
        printf("%i %u\n",i,rcCurve[i]);
    }
}

 //printf("%i %u\n",d,stepTable[d]);
 //printf("%i %u\n",i,rcCurve[i]);

uint16_t adsrNext(uint32_t* currStepQ16,uint16_t dur)  
{
    if (dur >= DUR_MAX)
        dur = DUR_MAX - 1;

    // step en Q16
    uint32_t stepQ16 = stepTableQ16[dur];

    // avance de step
    uint32_t next = *currStepQ16 + stepQ16;

    // clamp à la fin de la courbe RC
    uint32_t maxQ16 = (SAMPLES_NB - 1) << 16;
    if (next > maxQ16)
        next = maxQ16;

    *currStepQ16 = next;

    // conversion Q16 → index dans rcTable
    uint32_t idx = next >> 16;
    if (idx >= SAMPLES_NB)
        idx = SAMPLES_NB - 1;

    return (uint16_t)idx;
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

void __not_in_flash_func(adsrHandler)()
{
    if((millisCounter-adsrTime)>adsrTimingInterval){
        adsrTime=millisCounter;

        for(uint8_t a=0;a<MAX_ADSR;a++)
        {
            uint16_t* ov=&adsrOutputsValues[a];
            uint16_t  ov0;
            uint8_t*  as=&adsrStatus[a];
            uint16_t* ap=&adsrScopeBufPtr[a];
            if (__builtin_expect(*as != ADSR_OFF, 0)) {

                uint8_t   out_id;
                uint16_t  lev=amplLevel[adsrCoderLev[a]];
                uint32_t  cx=0;
                uint32_t* ce=&adsrCurrEch[a];              
                #define P15 (1<<15) 
                
                printf("|s:%u x:%u/",*as,cx);

                switch(*as){
                    case ADSR_ATT:
                        
                        cx=adsrNext(ce, adsrCoderAtt[a]);
                        *ov=rcCurve[cx];
                        printf("%u ",cx);
                        if (cx == SAMPLES_NB - 1) { *ce = 0; *as = ADSR_DEC;}; 
                        break;
                    case ADSR_DEC:
                        // valeurs 1-x

                        cx=adsrNext(ce, adsrCoderDec[a]);
                        ov0=rcCurve[cx];
                        *ov  = P15 - (P15 - lev) * ov0 / P15;
                        printf("%u ",cx);
                        if (cx == SAMPLES_NB - 1) { *ce = 0; *as = ADSR_SUS;}; 
                        break;
                    case ADSR_SUS:

                        cx=adsrNext(ce, adsrCoderSus[a]);  // timing management
                        printf("%u ",cx);
                        if (cx == SAMPLES_NB - 1) { *ce = 0; *as = ADSR_REL;} 
                        *ov=lev;
                        break;
                    case ADSR_REL:            
                        // valeurs 1-x

                        cx=adsrNext(ce, adsrCoderRel[a]);
                        ov0=rcCurve[cx];
                        *ov  = lev - lev * ov0 / P15;
                        printf("%u ",cx);
                        if (cx == SAMPLES_NB - 1) { *ce = 0; *as = ADSR_OFF;}
                        break;
                    default:break;
                }
                
                printf("A:%u:%u cx:%u ov:%u pt:%u t:%u,%u,%u,%u\n",a,*as,cx,*ov,*ap,adsrCoderAtt[a],adsrCoderDec[a],adsrCoderSus[a],adsrCoderRel[a]);       
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