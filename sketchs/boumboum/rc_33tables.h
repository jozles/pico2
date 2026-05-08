#pragma once
#include <stdint.h>

#define RC_N_SAMPLES 1024
#define RC_N_TABLES  33
#define SIN 1
#define TRI 1
#define SAW 0
#define RC_N_WAVES  2


extern int16_t rc_tables[RC_N_TABLES][RC_N_SAMPLES][RC_N_WAVES];

