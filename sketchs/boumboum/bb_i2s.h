#ifndef _SYNTH_H_
#define _SYNTH_H_   

#include "pico/stdlib.h"

void i2s_start(bool on_off);

int i2s_active_dma();
int i2sSetup(PIO pio,uint8_t i2sDataPin,int32_t* buf[2]);


#endif  //_SYNTH_H_ 