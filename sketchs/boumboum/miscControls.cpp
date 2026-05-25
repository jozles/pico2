#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "const.h"
#include "miscControls.h"
#include "input_tables_management.h"

uint16_t adsrAttCoder[MAX_ADSR];
uint16_t adsrDecCoder[MAX_ADSR];
uint16_t adsrSusCoder[MAX_ADSR];
uint16_t adsrRelCoder[MAX_ADSR];
uint16_t adsrLevCoder[MAX_ADSR];
uint8_t  adsrStatus[MAX_ADSR];                          // voir AdsrStates
uint32_t adsrCurrEch[MAX_ADSR];
uint32_t adsrCurrEchFra[MAX_ADSR];
uint16_t adsrStepInt[MAX_ADSR][ADSR_MAX_STATES];
uint16_t adsrStepFra[MAX_ADSR][ADSR_MAX_STATES];
int16_t  adsrOutputsValues[MAX_ADSR][MAX_OUTPUTS_PER_OBJ];
int16_t  adsr_ctl_input_id[MAX_ADSR][MAX_OUTPUTS_PER_OBJ];
int16_t  adsr_ctl_output_id[MAX_ADSR][MAX_INPUTS_PER_OBJ];

uint32_t    adsrTime=0;
uint32_t    adsrTimingInterval=1000/ADSR_SAMPLE_RATE;

extern uint32_t millisCounter;
extern uint16_t amplLevel[];
extern int16_t  ctl_output_id_chain[];

void adsrInit()
{
    for(uint8_t a=0;a<MAX_ADSR;a++){
        adsrAttCoder[a]=1;
        adsrDecCoder[a]=1;
        adsrSusCoder[a]=10;
        adsrRelCoder[a]=100;
        adsrLevCoder[a]=ADSR_MAX_LEVEL_CODERS/2;
        adsrStatus[a]=ADSR_OFF;
    }
}

void __not_in_flash_func(adsrEchTime)(uint8_t a,uint8_t* state,uint32_t* ce,uint32_t* cf)
{
    if(*ce >= ((BASIC_WAVE_TABLE_LEN / 4)-1)){*ce=0;}   // nouveau status
    *cf+=adsrStepFra[a][*state];
    uint32_t carry = (*cf >= MAX_STEP_FRA);
    *cf -= carry * MAX_STEP_FRA;
    *ce += adsrStepInt[a][*state] + carry;
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
            uint8_t*  as=&adsrStatus[a];
            if (__builtin_expect(*as != ADSR_OFF, 0)) {

                uint8_t   out_id;
                uint16_t  lev=amplLevel[adsrLevCoder[a]];
                uint32_t  cx=0;
                uint32_t* ce=&adsrCurrEch[a];
                int16_t*  ov=&adsrOutputsValues[a][LSIN];
                

                // on utilise la cr table sinus n°32 dans ses 1er 90° donc BASIC_WAVE_TABLE_LEN / 4 samples            
                adsrEchTime(a,as,ce,&adsrCurrEchFra[a]);

                switch(*as){
                    case ADSR_ATT:
                        *ov=rc_tables[32][*ce][LSIN];        
                        break;
                    case ADSR_DEC:
                        // valeurs 1-x
                        cx=(rc_tables[32][*ce][LSIN]);
                        *ov=2^15-(cx*lev/2^15);
                        break;
                    case ADSR_SUS:
                        // on utilise amplLevel[adsrLevCoder]
                        *ov=lev;
                        break;
                    case ADSR_REL:            
                        // valeurs 1-x
                        cx=(rc_tables[32][*ce][LSIN]);
                        *ov=lev-(cx*lev/2^15);
                        break;
                    default:break;
                }
            
                out_id=ctl_output_id_chain[adsr_ctl_output_id[a][LSIN]];
                if (__builtin_expect(out_id != NO_LINK, 0)){update_inputs(out_id,*ov);}
            }
        }
    }
}