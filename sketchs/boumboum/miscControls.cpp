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


void adsrInit()
{
    for(uint8_t a=0;a<MAX_ADSR;a++){
        adsrCoderAtt[a]=10;//setAdsrDur(a,ADSR_ATT,adsrCoderAtt[a]);
        adsrCoderDec[a]=32;//setAdsrDur(a,ADSR_DEC,adsrCoderDec[a]);
        adsrCoderSus[a]=32;//setAdsrDur(a,ADSR_SUS,adsrCoderSus[a]);
        adsrCoderRel[a]=48;//setAdsrDur(a,ADSR_REL,adsrCoderRel[a]);
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

#define ONE_Q16     65536
#define ONE_Q16_11  72440 // 65536 * 1.1
#define RC_SAMPLES_NB  2048
#define P16 (1<<16) 

const uint32_t yTableQ16[DUR_ECH_NB] = {
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

uint32_t stepTableQ16[DUR_ECH_NB];     // coefficient RC par durée (Q16)
uint16_t rcCurve[RC_SAMPLES_NB]; // courbe RC 0..65535

// Paramètres modifiables
uint32_t gammaQ16 = 32768;   // ≈0.6  courbure durée (0.3–2.0)
uint32_t betaQ16  = 4096;   // ≈0.5  courbure RC (0.3–2.0)

uint32_t stepMinQ16 = 32768;                 // 0.5 en Q16
uint32_t stepMaxQ16 = (RC_SAMPLES_NB << 16);    // RC_SAMPLES_NB en Q16

void fillDur(void)
{
    const uint32_t stepMaxQ16 = (RC_SAMPLES_NB << 16); // 2048 << 16
    const uint32_t stepMinQ16 = 32768;             // 0.5 << 16

    const uint32_t yMax = yTableQ16[DUR_ECH_NB-1];    // 65532

    for (uint32_t d = 0; d < DUR_ECH_NB; d++) {
        uint32_t y = yTableQ16[d];
        if (y == 0) y = 1;

        // step en Q16 = (RC_SAMPLES_NB / y) en Q0
        // donc stepQ16 = (RC_SAMPLES_NB << 16) / y
        stepTableQ16[d] = ((uint32_t)RC_SAMPLES_NB << 16) / y;
        printf("%u %u\n",d,stepTableQ16[d]);
    }  


    // 2) rcCurve : approximation tension condensateur RC
    uint32_t yQ16 = 0;
    //uint32_t kQ16 = (uint32_t)(229900u / RC_SAMPLES_NB);       // 229900u equiv 97% du sommet de la courbe au dernier step
    //uint32_t kQ16 = (uint32_t)(196608u / RC_SAMPLES_NB);       // 229900u equiv 95% du sommet de la courbe au dernier step
    //uint32_t kQ16 = (uint32_t)(174240u / RC_SAMPLES_NB);       // 229900u equiv 93% du sommet de la courbe au dernier step    
    //uint32_t kQ16 = (uint32_t)(157696u / RC_SAMPLES_NB);       // 229900u equiv 91% du sommet de la courbe au dernier step  

    
/*
// formule générale pour obtenir la table ; 
double coeff = (double)(P16 - 1) / (double)P16;   // 65535/65536
double k = 1.0 - pow(1.0 - coeff, 1.0 / RC_SAMPLES_NB);
uint32_t kQ16 = (uint32_t)(k * 65536.0);*/

    uint32_t kQ16 = 77;   // Pour coeff =0.91 et 2048 samples : 𝑘 = 1-0,091/2048≈0,0011756 𝑘𝑄16≈0,0011756*65536≈77

    for (int i = 0; i < RC_SAMPLES_NB; i++)
    {
        uint32_t diff = ONE_Q16_11 - yQ16;
        uint32_t dy   = (uint32_t)(((uint64_t)diff * kQ16) >> 16);
        yQ16 += dy;
        if (yQ16 > ONE_Q16_11) yQ16 = ONE_Q16_11;

        rcCurve[i] = (uint16_t)((yQ16 * 65535ULL) >> 16);
        printf("%i %u\n",i,rcCurve[i]);
    }
}

 //printf("%i %u\n",d,stepTable[d]);
 //printf("%i %u\n",i,rcCurve[i]);

uint16_t adsrNext(uint32_t* currEchQ16,uint16_t dur)  
{
    if (dur >= DUR_ECH_NB)
        dur = DUR_ECH_NB - 1;

    uint32_t stepQ16 = stepTableQ16[dur];

    // avance
    uint32_t next = *currEchQ16 + stepQ16;

    // clamp fin de courbe
    uint32_t maxQ16 = ((RC_SAMPLES_NB-1) << 16);
    if (next >= maxQ16)
        next = maxQ16;

    *currEchQ16 = next;

    // conversion Q16 → index entier
    return (uint16_t)(next >> 16);
}

void __not_in_flash_func(setAdsrLev)(uint8_t adsr,int32_t val)
{
    adsrCoderLev[adsr]=val;
}

void __not_in_flash_func(setAdsrDur)(uint8_t adsr,uint8_t adsrStatus,int32_t val)
{

}


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
                uint32_t* ce=&adsrCurrEch[a];            
                uint32_t  cx=*ce>>16;   // prev currEch  
                
                printf("|s:%u ",*as);

                switch(*as){
                    case ADSR_ATT:
                        cx=adsrNext(ce,adsrCoderAtt[a]);   // update adsrNext 
                        *ov=rcCurve[cx];                                               
                        printf("x:%u ",cx);
                        if (cx == RC_SAMPLES_NB-1) { *ce = 0; *as = ADSR_DEC;}
                        break;
                    case ADSR_DEC:
                        // valeurs 1-x
                        cx=adsrNext(ce,adsrCoderDec[a]);
                        ov0=rcCurve[cx];
                        //*ov = P16 - (((uint32_t)(P16 - lev) * ov0) >> 16);   //
                        *ov  = (uint16_t)((uint32_t)P16 - ((uint32_t)(P16 - lev) * (uint32_t)ov0)/ (uint32_t)P16);
                        printf("x:%u ",cx);

                        if (cx == RC_SAMPLES_NB-1) {*ce = 0; *as = ADSR_SUS;}                        
                        break;
                    case ADSR_SUS:
                        cx=adsrNext(ce,adsrCoderSus[a]);  // timing management                    
                        *ov=lev;
                        printf("x:%u ",cx);

                        if (cx == RC_SAMPLES_NB-1) { *ce = 0; *as = ADSR_REL;}
                        break;
                    case ADSR_REL:            
                        // valeurs 1-x
                        {cx=adsrNext(ce,adsrCoderRel[a]);
                        ov0=rcCurve[cx];
                        //*ov = lev - (((uint64_t)lev * ov0) >> 15);   
                       *ov = (uint16_t)((uint32_t)lev - (uint32_t)((uint32_t)lev * (uint32_t)ov0) / (uint32_t)P16);

                        printf("x:%u ",cx);

                        if (cx == RC_SAMPLES_NB-1) { *ce = 0; *as = ADSR_OFF;ov=0;} 
                        }break;
                    default:break;
                }
                
                printf("A:%u:%u o:%u/%u",a,*as,ov0,*ov);
                //printf("pt:%u t:%u,%u,%u,%u",*ap,adsrCoderAtt[a],adsrCoderDec[a],adsrCoderSus[a],adsrCoderRel[a]); 
                printf("\n");      
                out_id=ctl_output_id_chain[adsr_ctl_output_id[a]];
                if (__builtin_expect(out_id != NO_LINK, 0)){update_inputs(out_id,*ov);}

                adsrScopeBufReal[a*ADSR_SCOPE_BUFFER_LEN + *ap]=*ov;
                *ap++;if(__builtin_expect(*ap>ADSR_SCOPE_BUFFER_LEN,0)){*ap=0;}
            }
            else if(__builtin_expect(*ap!=0 && *ap<ADSR_SCOPE_BUFFER_LEN,0)){
                int32_t* ab=&adsrScopeBufReal[a*ADSR_SCOPE_BUFFER_LEN];
                for(uint16_t x=*ap;x<ADSR_SCOPE_BUFFER_LEN;x++){*(ab+x)=0;}
                *ap=0;
            }

        }
    }
}