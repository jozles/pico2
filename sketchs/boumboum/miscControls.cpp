#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "const.h"
#include "miscControls.h"

uint16_t adsrAttCoder[MAX_ADSR];                       // current lfo freq
uint16_t adsrDecCoder[MAX_ADSR];
uint16_t adsrSusCoder[MAX_ADSR];
uint16_t adsrRelCoder[MAX_ADSR];
uint16_t adsrLevCoder[MAX_ADSR];
int16_t  adsrOutputsValues[MAX_ADSR][MAX_OUTPUTS_PER_OBJ];
int16_t  adsr_ctl_input_id[MAX_ADSR][MAX_OUTPUTS_PER_OBJ];
int16_t  adsr_ctl_output_id[MAX_ADSR][MAX_INPUTS_PER_OBJ];