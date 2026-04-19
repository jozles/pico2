#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "const.h"
#include "miscControls.h"

uint16_t adsrAttCoder[ADSR_NB];                       // current lfo freq
uint16_t adsrDecCoder[ADSR_NB];
uint16_t adsrSusCoder[ADSR_NB];
uint16_t adsrRelCoder[ADSR_NB];
uint16_t adsrLevCoder[ADSR_NB];
