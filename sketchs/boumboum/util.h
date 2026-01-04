#ifndef _UTIL_H_
#define _UTIL_H_ 

#include "hardware/pio.h"

void setup();
void pio_full_reset(PIO pio);
void dump_pio_instr(PIO pio, const char *label);
void dumpStr(int32_t* str,uint32_t nb);
void debug_gpio3_function(void);
void dumpStr16(int32_t* str);

void next_sound_feeding(int32_t* next_sound,uint32_t next_sound_size);

#endif  //_UTIL_H_