#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "const.h"
#include "mapping.h"

const char inputs_names[][IN_OUT_NAME_LEN]={
    #define X(name,text) text,
    #include "inputs.def"
    #undef X
};

const char outputs_names[][IN_OUT_NAME_LEN]={
    #define Y(name,text) text,
    #include "outputs.def"
    #undef Y
};

uint8_t  inputs[INPUTS_NB];

void inputsInit(){
    memset(inputs,0x00,INPUTS_NB);
}