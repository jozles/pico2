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


void adsrInit()
{
    for(uint8_t a=0;a<MAX_ADSR;a++){
        adsrCoderAtt[a]=1;
        adsrCoderDec[a]=1;
        adsrCoderSus[a]=10;
        adsrCoderRel[a]=100;
        adsrCoderLev[a]=ADSR_MAX_LEVEL_CODERS/2;
        adsrStatus[a]=ADSR_OFF;
        adsrScopeBufPtr[a]=0;

        adsrCoderAttAtt[a]=FULL_ATTENUATION_VALUE;
        adsrCoderDecAtt[a]=FULL_ATTENUATION_VALUE;
        adsrCoderSusAtt[a]=FULL_ATTENUATION_VALUE;
        adsrCoderRelAtt[a]=FULL_ATTENUATION_VALUE;
        adsrCoderLevAtt[a]=FULL_ATTENUATION_VALUE;
    }
}

void __not_in_flash_func(setAdsrLev)(int32_t val,uint8_t adsr)
{
    adsrCoderLev[adsr]=val;
}

void __not_in_flash_func(setAdsrDur)(int32_t val,uint16_t* what)
{
    *what=val;
}

void __not_in_flash_func(adsrEchTime)(uint8_t adsr_nb,uint8_t* state,uint32_t* ce,uint32_t* cf)
{
    if(*ce >= ((BASIC_WAVE_TABLE_LEN / 4)-1)){*ce=0;}   // nouveau status
    *cf+=adsrStepFra[adsr_nb][*state];
    uint32_t carry = (*cf >= MAX_STEP_FRA);
    *cf -= carry * MAX_STEP_FRA;
    *ce += adsrStepInt[adsr_nb][*state] + carry;
    if(*ce >= ((BASIC_WAVE_TABLE_LEN / 4)-1)){
        *ce=(BASIC_WAVE_TABLE_LEN / 4);
        *state++;};                                     // changement de status
}

void __not_in_flash_func(adsrHandler)()
{
    if((millisCounter-adsrTime)>adsrTimingInterval){
        adsrTime=millisCounter;

        for(uint8_t a=0;a<MAX_ADSR;a++)
        {
            int16_t*  ov=&adsrOutputsValues[a];
            uint8_t*  as=&adsrStatus[a];
            if (__builtin_expect(*as != ADSR_OFF, 0)) {

                uint8_t   out_id;
                uint16_t  lev=amplLevel[adsrCoderLev[a]];
                uint32_t  cx=0;
                uint32_t* ce=&adsrCurrEch[a];              
                #define P15 (1<<15)         

                // on utilise la cr table sinus n°32 dans ses 1er 90° donc BASIC_WAVE_TABLE_LEN / 4 samples ; valeurs 0 à 0x7fff         
                adsrEchTime(a,as,ce,&adsrCurrEchFra[a]);

                switch(*as){
                    case ADSR_ATT:
                        *ov=rc_tables[32][*ce][LSIN]; 
                        printf("ov:%d ptr:%u\n",*ov,adsrScopeBufPtr[0]);       
                        break;
                    case ADSR_DEC:
                        // valeurs 1-x
                        cx=(rc_tables[32][*ce][LSIN]);
                        *ov=P15-(cx*lev/P15);
                        break;
                    case ADSR_SUS:
                        // on utilise amplLevel[adsrLevCoder]
                        *ov=lev;
                        break;
                    case ADSR_REL:            
                        // valeurs 1-x
                        cx=(rc_tables[32][*ce][LSIN]);
                        *ov=lev-(cx*lev/P15);
                        break;
                    default:break;
                }
            
                out_id=ctl_output_id_chain[adsr_ctl_output_id[a]];
                if (__builtin_expect(out_id != NO_LINK, 0)){update_inputs(out_id,*ov);}

            }
            adsrScopeBufReal[a*ADSR_SCOPE_BUFFER_LEN + adsrScopeBufPtr[a]]=*ov;
            adsrScopeBufPtr[a]++;if(__builtin_expect(adsrScopeBufPtr[a]>ADSR_SCOPE_BUFFER_LEN,0)){adsrScopeBufPtr[a]=0;}
        }
    }
}