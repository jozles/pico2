#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "const.h"
#include "miscControls.h"

uint16_t adsrAttCoder[MAX_ADSR];
uint16_t adsrDecCoder[MAX_ADSR];
uint16_t adsrSusCoder[MAX_ADSR];
uint16_t adsrRelCoder[MAX_ADSR];
uint16_t adsrLevCoder[MAX_ADSR];
uint8_t  adsrStatus[MAX_ADSR];                          // voir AdsrStates
uint32_t adsrCurrEch[MAX_ADSR];
uint32_t adsrCurrEchFra[MAX_ADSR];
uint16_t adsrStepInt[MAX_ADSR];
uint16_t adsrStepFra[MAX_ADSR];
int16_t  adsrOutputsValues[MAX_ADSR][MAX_OUTPUTS_PER_OBJ];
int16_t  adsr_ctl_input_id[MAX_ADSR][MAX_OUTPUTS_PER_OBJ];
int16_t  adsr_ctl_output_id[MAX_ADSR][MAX_INPUTS_PER_OBJ];

uint32_t    adsrTime=0;
uint32_t    adsrTimingInterval=1000/ADSR_SAMPLE_RATE;

extern uint32_t millisCounter;

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

void __not_in_flash_func(adsrEchTime)(uint8_t a,uint32_t* ce,uint32_t* cf)
{
    if(*ce >= ((BASIC_WAVE_TABLE_LEN / 4)-1)){*ce=0;}
    *cf+=adsrStepFra[a];
    uint32_t carry = (*cf >= MAX_STEP_FRA);
    *cf -= carry * MAX_STEP_FRA;
    *ce += adsrStepInt[a] + carry;
    if(*ce >= ((BASIC_WAVE_TABLE_LEN / 4)-1)){
        *ce=(BASIC_WAVE_TABLE_LEN / 4);
        adsrStatus[a]++;};    
}

void __not_in_flash_func(adsrHandler)()
{
    if((millisCounter-adsrTime)>adsrTimingInterval){
        adsrTime=millisCounter;

        for(uint8_t a=0;a<MAX_ADSR;a++)
        {
            switch(adsrStatus[a]){
                case ADSR_OFF:break;
                case ADSR_ATT:
                    adsrEchTime(a,&adsrCurrEch[a],&adsrCurrEchFra[a]);
                    // on utilise la cr table sinus n°32 dans ses 1er 90° donc BASIC_WAVE_TABLE_LEN / 4 samples
                    break;
                case ADSR_DEC:
                    adsrEchTime(a,&adsrCurrEch[a],&adsrCurrEchFra[a]);
                    // on utilise la cr table sinus n°32 dans ses 1er 90° donc BASIC_WAVE_TABLE_LEN / 4 samples
                    // valeurs 1-x
                    break;
                case ADSR_SUS:break;
                case ADSR_REL:
                    adsrEchTime(a,&adsrCurrEch[a],&adsrCurrEchFra[a]);
                    // on utilise la cr table sinus n°32 dans ses 1er 90° donc BASIC_WAVE_TABLE_LEN / 4 samples
                    // valeurs 1-x
                    break;
                default:break;
            }
        }
    }
}