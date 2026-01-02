#include <stdio.h>
#include "pico/stdlib.h"
#include "const.h"
#include "oscillator.h"
#include <math.h>


uint32_t sampleCounter=0;


uint16_t samplePtr(float req_freq,uint32_t from_start_millis_counter){

    //tests oscillateur    
    //uint32_t a=970200; // 22 secondes maxi

    float bs,ds;

    uint16_t ez;

        bs=from_start_millis_counter*req_freq*SAMPLE_PER;

        ds=bs-(float)((uint32_t)bs);

//Serial.print(SAMPLE_PER,10);Serial.print(' ');Serial.print(bs,6);Serial.print(' ');Serial.print(ds,6);Serial.print(' ');

        ez=(uint16_t)(ds*BASIC_WAVE_TABLE_LEN);

    return ez;   
}

uint16_t samplePtr(uint32_t req_freqx100,uint32_t from_start_millis_counter){

// fsmc 1024 mS maxi rf 20.00+-0.01 mini 12000+-6

    float ds;

    uint16_t ez;

        uint64_t bs=from_start_millis_counter*req_freqx100*SAMPLE_PER;

        ds=bs-(float)((uint32_t)bs);

//Serial.print(SAMPLE_PER,10);Serial.print(' ');Serial.print(bs,6);Serial.print(' ');Serial.print(ds,6);Serial.print(' ');

        ez=(uint16_t)(ds*BASIC_WAVE_TABLE_LEN);

    return ez;   
}

