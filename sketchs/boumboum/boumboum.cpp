#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "const.h"
#include "coder.h"
#include "util.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "bb_i2s.h"
#include "test.h"
#include "frequences.h"
#include "leds.h"
#include "st7789.h"
#include "menus.h"

const char* version=VERSION;
uint8_t currVoice=0;

volatile bool voicesSw[CODER_NB];                   // coder it handler scans all physical coders

#define MENU0_CODER_NB 1
volatile int16_t menu0Coders[]={0};                 // [0] curr input nb
uint16_t menu0MaxCoders[]={MENU0_NB-1};

extern const char menu0_names[][MENU_NAME_LEN];

#define LFO_VAR_NB 2
volatile int16_t menuLfosCoders[LFO_VAR_NB+1];      // [0] curr input nb ; [1] curr coder value for freq ; [2] curr coder value for rc
uint16_t menuMaxLfosCoders[]={LFOS_NB-1,LFOS_MAX_FREQ_CODERS,MAXCODER_RC};
uint16_t* lfosVar[CODER_NB];                        // inits dans menu.cpp ; les éléments inutilisés sont nullptr                

int main() {

    stdio_init_all();
    sleep_ms(2000);
    printf("\n+boumboum\n");    // %s\n",version);
    
if (watchdog_caused_reboot()) {
    printf("RESET = WATCHDOG\n");
} else {
    printf("RESET = NORMAL\n");
}
    setup();
    init_test_7789(20,25*8,0,TFT_H-12*8,TFT_H,1);       // init screen animation

    i2s_start();                                        // launch sound output

    menus_init();

    while(1){
        uint8_t menu=coders_for_menu("boumboum ",(const char*)menu0_names,MENU0_NB,MENU_NAME_LEN,MENU0,menu0Coders,voicesSw,menu0MaxCoders,nullptr,0,MENU0_CODER_NB);

        switch(menu){
            case VOICES_FR: currVoice=coders_for_freq(currVoice);break;
            case WAVES_AMP: coders_for_wavesAmpl(currVoice);break;
            case GEN_AMPL_: coders_for_genAmpl(currVoice);break;
            case LFOS_____: coders_for_menu("lfo",(const char*)nullptr,LFOS_NB,0,LFOS,menuLfosCoders,voicesSw,menuMaxLfosCoders,lfosVar,LFO_VAR_NB,BASIC_WAVES_NB);break;
            case MAPPING__: coders_for_mapping();break;

            default:break;
        }
    }
}
