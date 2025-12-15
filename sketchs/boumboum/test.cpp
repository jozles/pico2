#include "pico/stdlib.h"
#include "const.h"
#include "util.h"
#include "bb_i2s.h"
#include <stdio.h>
#include <math.h> 
#include <cstring>  
#include "frequences.h"

static uint32_t sample_buffer[SAMPLE_BUFFER_SIZE*2]; // 4 bytes per sample, 2 channels

extern volatile uint32_t millisCounter;

extern volatile uint32_t durOffOn[];
extern volatile bool led;
extern volatile uint32_t ledBlinker;

extern int16_t sineWaveform[WFSTEPNB];
extern volatile bool i2s_hungry;        // indique que le buffer courant est copié dans le buffer de dma ; donc préparer la suite
extern int32_t* audio_data;             // pointeur du buffer courant
extern uint32_t i2s_error; 
extern int32_t* i2s_s;
extern int32_t* i2s_a;
extern uint32_t i2s_c; 

//void testSample(float freq,uint16_t ampl){}

float testSample(int16_t freq_lin,uint16_t ampl)
{
    float freq_snd=calcFreq(freq_lin);

    //sleep_ms(5000);
    printf("test_sample _ freq_lin:%5d freq_snd:%5.2f ampl:%d\r",freq_lin,freq_snd,ampl);

    uint8_t h_cnt=0;

    uint16_t k=0,ech=0;

    //printf("filling sample buffer ...\n");

    for(uint32_t i=0;i<SAMPLE_BUFFER_SIZE;i++){

        sample_buffer[i*2]= (ampl*sineWaveform[ech]); // Left channel
        sample_buffer[i*2+1]= sample_buffer[i*2]; // Right channel

        //printf("i:%4d k:%d sine_ech_nb:%04d sample:%08x\n",i,k,ech,sample_buffer[i*2]);

        ech+=32;if(ech>=WFSTEPNB){ech-=WFSTEPNB;}           // @32 : 2048/32=64 échantillons par période @44100 : 44100/64=689 Hz
    }
    return freq_snd;
}

void test_next_sound_feeding(int32_t* next_sound,uint32_t next_sound_size){
    memcpy(next_sound,sample_buffer,next_sound_size*2*4); // 4 bytes per sample, 2 channels
}