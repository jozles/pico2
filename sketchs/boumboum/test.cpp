#include "pico/stdlib.h"
#include "const.h"
#include "bb_i2s.h"
#include <stdio.h>
#include <math.h>   
#include "frequences.h"

extern volatile uint32_t millisCounter;

extern volatile uint32_t durOffOn[];
extern volatile bool led;
extern volatile uint32_t ledBlinker;

extern int16_t sineWaveform[WFSTEPNB];
extern bool i2s_hungry;                 // indique que le buffer courant est copié dans le buffer de dma ; donc préparer la suite
extern int32_t* audio_data;             // pointeur du buffer courant

void testSample(float freq,uint8_t ampl)
{

    uint64_t intFreq=(uint64_t)(freq*FREQUENCY_DECIM);
    uint64_t locRate=(uint64_t)SAMPLE_RATE*FREQUENCY_DECIM;

     sleep_ms(5000);printf("freq:%5.4f intF:%lu locRate:%lu ampl:%d\n",freq,intFreq,locRate,ampl);

    int32_t sample_buffer[SAMPLE_BUFFER_SIZE*2]; // 4 bytes per sample, 2 channels

    uint64_t ech,ech0,ech1,k=0; 
    
    for(uint32_t i=0;i<SAMPLE_BUFFER_SIZE;i++){
   
        ech=((uint64_t)(i*WFSTEPNB*intFreq)/locRate)-(k*WFSTEPNB);
        ech0=(i*WFSTEPNB*intFreq);
        ech1=((uint64_t)(i*WFSTEPNB*intFreq)/locRate);
        printf("ech:%04lu 0:%09lu 1:%09lu wf:%d \n",ech,ech0,ech1,WFSTEPNB);
        if(ech>=2048){k++;ech-=WFSTEPNB;}

        sample_buffer[i*2]= (ampl * sineWaveform[ech]); // Left channel
        sample_buffer[i*2+1]= sample_buffer[i*2]; // Right channel

        printf("i:%d sine_ech_nb:%04d k:%d sine_value:%d sample:%d\n",i,ech,k,sineWaveform[ech],sample_buffer[i*2]);
    }

    while(1){

        LEDBLINK

        if(i2s_hungry){
            audio_data=(int32_t*)sample_buffer;
            i2s_hungry=false;
        }
    }
}


