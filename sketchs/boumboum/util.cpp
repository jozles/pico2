#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "bb_i2s.h"
#include "const.h"
#include "util.h"
#include "coder.h"
#include "frequences.h"
#include "test.h"
#include "leds.h"
#include "st7789.h"

// boumboum

static int st_dma_channel;
static int ws_dma_channel;

extern struct voice voices[];

volatile uint8_t what=0;

#define W_TEST 1    // test simple ; sinus continu depuis buffer rempli une fois (1/32)
#define W_SINUS 2   // sinus continu calculé à la volée

extern volatile uint32_t millisCounter;

volatile uint32_t durOffOn[]={LEDOFFDUR,LEDONDUR};
volatile bool led=false;
volatile uint32_t ledBlinker=0;

struct repeating_timer millisTimer;

extern volatile int32_t coder1Counter;

bool millisTimerHandler(struct repeating_timer *t){
    millisCounter++;
    coderTimerHandler();
    return true;
}

void dump_pio_instr(PIO pio, const char *label) {
    printf("%s pio#%p:\n", label, pio);
    for (int i = 0; i < 32; i++) {
        printf("instr[%02d] = 0x%04x\n", i, pio->instr_mem[i]);
    }
}

void debug_gpio3_function(void) {
    uint func = gpio_get_function(3);
    printf("GPIO3 function = %u\n", func);
}

void pio_full_reset(PIO pio) {

    printf("pio#%p full reset\n",pio);

    // 1. Désactiver toutes les SM
    for (int sm = 0; sm < 4; sm++) {
        pio_sm_set_enabled(pio, sm, false);
    }

    // 2. Libérer toutes les SM dans le SDK
    for (int sm = 0; sm < 4; sm++) {
        pio_sm_unclaim(pio, sm);
    }

    // 3. Effacer la mémoire d’instructions PIO
    pio_clear_instruction_memory(pio);

    // 4. Redémarrer les diviseurs de clock
    for (int sm = 0; sm < 4; sm++) {
        pio_sm_clkdiv_restart(pio, sm);
    }

    // 5. Reset interne complet des SM
    for (int sm = 0; sm < 4; sm++) {
        pio_sm_restart(pio, sm);
    }

    //printf("Dump PIO0:\n");
    //for (int i = 0; i < 32; i++) {
    //    printf("instr[%02d] = 0x%04x\n", i, pio0->instr_mem[i]);
    //}

}

#ifdef GLOBAL_DMA_IRQ_HANDLER
void global_dma_irq_handler(){
    uint32_t global_dma_irq_status = dma_hw->ints0;

    if((global_dma_irq_status&(1u << st_dma_channel))!=0){
        st_dma_irq_handler();
    }
    if((global_dma_irq_status&(1u << ws_dma_channel))!=0){
        ws_dma_irq_handler();
    }
}

void init_global_dma_irq(){
    irq_set_exclusive_handler(DMA_IRQ_1, global_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);
}
#endif


void setup(){

    //pio_full_reset(pio0);
    //pio_full_reset(pio1);

    gpio_init(LED);gpio_set_dir(LED,GPIO_OUT); gpio_put(LED,LOW);
    //gpio_init(LED_BLUE);gpio_set_dir(LED_BLUE,GPIO_OUT);gpio_put(LED_BLUE,LOW);
    //gpio_init(LED_RED);gpio_set_dir(LED_RED,GPIO_OUT);gpio_put(LED_RED,LOW);
    //gpio_set_dir(LED_GREEN,GPIO_OUT);gpio_put(LED_GREEN,LOW);

    gpio_init(TEST_PIN);gpio_set_dir(TEST_PIN,GPIO_OUT); gpio_put(TEST_PIN,LOW);

    gpio_init(PIN_DCDC_PSM_CTRL);gpio_set_dir(PIN_DCDC_PSM_CTRL, GPIO_OUT);
    gpio_put(PIN_DCDC_PSM_CTRL, 1); // PWM mode for less Audio noise
    
    coderInit(PIO_CLOCK,PIO_DATA,PIO_SW,PIO_VPP,CODER_TIMER_POOLING_INTERVAL_MS,CODER_STROBE_NUMBER);
    coderSetup(&coder1Counter);
    // irq timer
    add_repeating_timer_ms(1, millisTimerHandler, NULL, &millisTimer);

    fillBasicWaveForms();
    freq_start();

    uint8_t channel=0;
    voiceInit(440,&voices[channel]);

    what=W_SINUS;

    //bb_i2s_start();

    //ws_dma_channel=ledsWs2812Setup(ws2812_pio,WS2812_LED_PIN);

    //st_dma_channel=st7789_setup(ST7789_SPI_SPEED);
 
    #ifdef GLOBAL_DMA_IRQ_HANDLER
    void init_global_dma_irq();
    #endif

}

    // exclusively called by void i2s_callback_func() in bb_i2s.cpp 
    // and dependancies _ see bb_i2s.cpp
void next_sound_feeding(int32_t* next_sound,uint32_t next_sound_size){

        if(next_sound_size!=SAMPLES_PER_BUFFER){
            printf("next_sound_size:%d != SAMPLES_PER_BUFFER:%d\n",next_sound_size,SAMPLES_PER_BUFFER);
            LEDBLINK_ERROR;
            return;
        }

    switch (what){
        case W_TEST:    
        test_next_sound_feeding(next_sound,next_sound_size);
            break;

        case W_SINUS:
        fillVoiceBuffer(next_sound,&voices[0]);
            break;
        
        default:
            break;
    }
}


void dumpVal(uint32_t val){
    
    for(int i=0;i<4;i++){
        uint8_t v0=val>>(i*8)&0xff;
        printf("%02x",v0);
    }
    printf("\n");
}


void dumpStr16(int32_t* str){
    printf("%p    ",str);
    for(uint32_t i=0;i<16;i++){
        printf("%08x ",str[i]);
    }
    printf(" ");
    for(uint32_t i=0;i<16;i++){
        for(uint8_t j=0;j<4;j++){
            uint8_t v0=(str[i]>>((3-j)*8))&0x000000FF;
            if(v0>=0x20 && v0<0x7f){printf("%c",v0);}
            else{printf(".");}
        }
        printf(" ");
    }
    printf("\n");
}

void dumpStr(int32_t* str,uint32_t nb){
    for(uint32_t i=0;i<nb;i+=16){
        dumpStr16(&str[i]);
    }
    printf("\n");
}