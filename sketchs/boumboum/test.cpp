#include "pico/stdlib.h"
#include "const.h"
#include "util.h"
#include "bb_i2s.h"
#include <stdio.h>
#include <math.h>   
#include "frequences.h"

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

void testSample(float freq,uint8_t ampl)
{
    sleep_ms(5000);printf("test_sample _ freq:%5.4f ampl:%d\n",freq,ampl);

    uint8_t h_cnt=0;

    uint32_t sample_buffer[SAMPLE_BUFFER_SIZE*2]; // 4 bytes per sample, 2 channels

    uint16_t k=0,ech=0;

    printf("filling sample buffer ...\n");

    for(uint32_t i=0;i<SAMPLE_BUFFER_SIZE;i++){

        sample_buffer[i*2]= (ampl*sineWaveform[ech]); // Left channel
        sample_buffer[i*2+1]= sample_buffer[i*2]; // Right channel

        //printf("i:%4d k:%d sine_ech_nb:%04d sample:%08x\n",i,k,ech,sample_buffer[i*2]);

        ech+=8;if(ech>=WFSTEPNB){ech-=WFSTEPNB;}
    }
    
    printf("start feeding\n");

    while(1){

        if(i2s_hungry){

            i2s_hungry=false;                       // avant de charger le buffer suivant pour ne pas risquer d'effacer le prochain true de l'irq
            audio_data=(int32_t*)sample_buffer;     // audio_data!=nullptr indique à l'irq qu'il y a des données à envoyer
            
            if(h_cnt<3){
                printf("h_cnt:%d err:%x s:%p a:%p c:%d\n",h_cnt,i2s_error,i2s_s,i2s_a,i2s_c);
                printf("audio_data=sample_buffer\n");
                dumpStr(audio_data,i2s_c);
                printf("irq_buffer\n");
                dumpStr(i2s_s,i2s_c);
                h_cnt++;
            }
            //printf("o\n");
        }
    }
}


