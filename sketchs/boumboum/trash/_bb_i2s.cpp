/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <math.h>
#include <cstring>
#include "bb_i2s.h"
#include "const.h"
#include "util.h"

#include "hardware/pll.h"
#include "hardware/clocks.h"
#include "hardware/structs/clocks.h"
#include "hardware/pio.h"

#include "pico/stdlib.h"
#include "pico/audio.h"
#include "pico/audio_i2s.h"

extern int16_t sineWaveform[WFSTEPNB];

volatile bool i2s_hungry = true;                 // indique que le buffer courant est copié dans le buffer de dma ; donc préparer la suite
int32_t* audio_data=nullptr;                     // pointeur du buffer courant

volatile uint32_t i2s_error=0;
int32_t* i2s_s=nullptr;
int32_t* i2s_a=nullptr;
uint32_t i2s_c=0;




audio_buffer_pool_t *ap;
static bool decode_flg = false;
static constexpr int32_t DAC_ZERO = 1;

#define audio_pio __CONCAT(pio, PICO_AUDIO_I2S_PIO)

static audio_format_t audio_format = {
    .sample_freq = SAMPLE_RATE,
    .pcm_format = AUDIO_PCM_FORMAT_S32,
    .channel_count = AUDIO_CHANNEL_STEREO
};

static audio_buffer_format_t producer_format = {
    .format = &audio_format,
    .sample_stride = 8
};

static audio_i2s_config_t i2s_config = {
    .data_pin = PICO_AUDIO_I2S_DATA_PIN,
    .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
    .dma_channel0 = 0,
    .dma_channel1 = 1,
    .pio_sm = 0
};

void i2s_audio_deinit()
{
    decode_flg = false;

    audio_i2s_set_enabled(false);
    audio_i2s_end();

    audio_buffer_t* ab;
    ab = take_audio_buffer(ap, false);
    while (ab != nullptr) {
        free(ab->buffer->bytes);
        free(ab->buffer);
        ab = take_audio_buffer(ap, false);
    }
    ab = get_free_audio_buffer(ap, false);
    while (ab != nullptr) {
        free(ab->buffer->bytes);
        free(ab->buffer);
        ab = get_free_audio_buffer(ap, false);
    }
    ab = get_full_audio_buffer(ap, false);
    while (ab != nullptr) {
        free(ab->buffer->bytes);
        free(ab->buffer);
        ab = get_full_audio_buffer(ap, false);
    }
    free(ap);
    ap = nullptr;
}

audio_buffer_pool_t *i2s_audio_init(uint32_t sample_freq)
{
    audio_format.sample_freq = sample_freq;

    audio_buffer_pool_t *producer_pool = audio_new_producer_pool(&producer_format, 3, SAMPLES_PER_BUFFER);
    ap = producer_pool;

    bool __unused ok;
    const audio_format_t *output_format;

    output_format = audio_i2s_setup(&audio_format, &audio_format, &i2s_config);
    if (!output_format) {
        panic("PicoAudio: Unable to open audio device.\n");
    }

    ok = audio_i2s_connect(producer_pool);
    assert(ok);
    { // initial buffer data
        audio_buffer_t *ab = take_audio_buffer(producer_pool, true);
        int32_t *samples = (int32_t *) ab->buffer->bytes;
        for (uint i = 0; i < ab->max_sample_count; i++) {
            samples[i*2+0] = DAC_ZERO;
            samples[i*2+1] = DAC_ZERO;
        }
        ab->sample_count = ab->max_sample_count;
        give_audio_buffer(producer_pool, ab);
    }
    audio_i2s_set_enabled(true);

    decode_flg = true;
    return producer_pool;
}

void bb_i2s_start(float freq,uint8_t ampl){
    
    sleep_ms(5000);printf("test_sample _ freq:%5.4f ampl:%d\n",freq,ampl);

    uint8_t h_cnt=0;

    uint32_t sample_buffer[SAMPLE_BUFFER_SIZE*2]; // 4 bytes per sample, 2 channels

    uint16_t ech=0;

    printf("filling sample buffer ...\n");

    for(uint32_t i=0;i<SAMPLE_BUFFER_SIZE;i++){

        sample_buffer[i*2]= (ampl*sineWaveform[ech]); // Left channel
        sample_buffer[i*2+1]= sample_buffer[i*2]; // Right channel

        //printf("i:%4d sine_ech_nb:%04d sample:%08x\n",i,ech,sample_buffer[i*2]);

        ech+=32;if(ech>=WFSTEPNB){ech-=WFSTEPNB;}           // @32 : 2048/32=64 échantillons par période @44100 : 44100/64=689 Hz
    }
    
    printf("start feeding\n");

    ap = i2s_audio_init(SAMPLE_RATE);

    while(1){

        if(i2s_hungry){

            i2s_hungry=false;                       // avant de charger le buffer suivant pour ne pas risquer d'effacer le prochain true de l'irq
            audio_data=(int32_t*)sample_buffer;     // audio_data!=nullptr indique à l'irq qu'il y a des données à envoyer
            
            if(h_cnt<3){
                printf("h_cnt:%d err:%x s:%p a:%p c:%d\n",h_cnt,i2s_error,i2s_s,i2s_a,i2s_c);
                printf("audio_data=sample_buffer\n");
                dumpStr(audio_data,i2s_c*2);
                printf("irq_buffer\n");
                dumpStr(i2s_s,i2s_c*2);
                h_cnt++;
            }
            //printf("o\n");
        }
    }
}

extern "C" {
// callback from:
//   void __isr __time_critical_func(audio_i2s_dma_irq_handler)()
//   defined at my_pico_audio_i2s/audio_i2s.c
//   where i2s_callback_func() is declared with __attribute__((weak))
void i2s_callback_func()
{
    if (decode_flg && (audio_data!=nullptr)) {
        audio_buffer_t *buffer = take_audio_buffer(ap, false);

    if (buffer == NULL) { return; } 
    int32_t *samples = (int32_t *) buffer->buffer->bytes;
        
    i2s_s=samples;i2s_a=audio_data;i2s_c=buffer->max_sample_count;

    if(buffer->max_sample_count!=SAMPLES_PER_BUFFER){
        i2s_error=buffer->max_sample_count;
        return;
    }

    memcpy(samples,audio_data,(buffer->max_sample_count)*2*4); // 4 bytes per sample, 2 channels    





    buffer->sample_count = buffer->max_sample_count;
    give_audio_buffer(ap, buffer);
        
    audio_data=nullptr;
    i2s_hungry=true;
        

    }
}
}