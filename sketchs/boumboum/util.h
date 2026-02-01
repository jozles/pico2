#ifndef _UTIL_H_
#define _UTIL_H_ 

#include "hardware/pio.h"

void setup();
void print_diag();
void print_diag(char c);
void print_diag(char c,uint32_t gdis);

void pio_full_reset(PIO pio);

void dumpStr(int32_t* str,uint32_t nb);
void dumpStr16(int32_t* str);

void next_sound_feeding(int32_t* next_sound,uint32_t next_sound_size);

void global_dma_irq_handler();

void adsr(int32_t* ccb,int32_t ccb0);
void autoMixer(int32_t* ccb,int32_t ccb0);

#endif  //_UTIL_H_