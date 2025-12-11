#include <stdio.h>
#include "pico/stdlib.h"
#include "const.h"
#include "oscillator.h"
#include <math.h>


uint32_t sampleCounter=0;

//#define PI 3.1415926536


uint16_t samplePtr(float freq,uint32_t sc){

    //tests oscillateur    
    //uint32_t a=970200; // 22 secondes maxi

    float bs,ds;

    uint16_t ez;

        bs=sc*SAMPLE_PER*freq;

        ds=bs-(float)((uint32_t)bs);

//Serial.print(SAMPLE_PER,10);Serial.print(' ');Serial.print(bs,6);Serial.print(' ');Serial.print(ds,6);Serial.print(' ');

        ez=(uint16_t)(ds*WFSTEPNB);

    return ez;   
}

/*void testSample(uint32_t sC,float freq){

    fillSineWaveForms();

    //uint32_t a=970200; // 22 secondes maxi
    //float freq=1105;
    uint32_t b=(uint32_t)(round(SAMPLE_F/freq));
    
    printf("%d %d %5.4f \n",b,sC,freq);

    for(uint32_t i=sC;i<sC+b;i++){

        //uint32_t time0=micros();
        //uint16_t j=samplePtr(freq,i);
        float bs,ds;

        uint16_t j;

        bs=i*SAMPLE_PER*freq;

        ds=bs-(float)((uint32_t)bs);

//Serial.print(SAMPLE_PER,10);Serial.print(' ');Serial.print(bs,6);Serial.print(' ');Serial.print(ds,6);Serial.print(' ');

        j=(uint16_t)(ds*WFSTEPNB);
        
        //Serial.print(micros()-time0);
        printf("%d %d %d \n",i,j,sineWaveform[j]);
    }
}*/