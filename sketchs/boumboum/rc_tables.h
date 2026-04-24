#ifndef _RC_TABLES_H_
#define _RC_TABLES_H_

#include <stdint.h>

#define RC_N_TABLES 32
#define RC_N_SAMPLES 1024
#define RC_N_VOICES 3

extern const int16_t rc_tables[RC_N_TABLES][RC_N_SAMPLES][RC_N_VOICES];

#endif