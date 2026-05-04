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

#define VCE_VAR_NB 6
volatile int16_t menuVcesCoders[VCE_VAR_NB+1];      // [0] curr input nb ; [1] curr coder value for freq ; [2] curr coder value for rc ; [3] curr coder value for genAmpl
uint16_t menuMaxVcesCoders[]={MAX_VOICES-1,VCES_MAX_FREQ_CODERS,MAXCODER_RC,MAX_CTL_ATT,MAX_CTL_ATT,MAX_16B_LINEAR_VALUE-1};
uint16_t* vcesVar[CODER_NB];                        // inits dans menu.cpp ; les éléments inutilisés sont nullptr 

#define LFO_VAR_NB 5
volatile int16_t menuLfosCoders[LFO_VAR_NB+1];      // [0] curr input nb ; [1] curr coder value freq ; [2] curr coder value cr ; [3] att value freq ; [4] att value cr
uint16_t menuMaxLfosCoders[]={MAX_LFO-1,LFOS_MAX_FREQ_CODERS,MAXCODER_RC,MAX_CTL_ATT,MAX_CTL_ATT};
uint16_t* lfosVar[CODER_NB];                        // inits dans menu.cpp ; les éléments inutilisés sont nullptr                

#define ADSR_VAR_NB 6
volatile int16_t menuAdsrCoders[ADSR_VAR_NB+1];      // [0] curr input nb ; [1] curr coder value for freq ; [2] curr coder value for rc
uint16_t menuMaxAdsrCoders[]={MAX_LFO-1,ADSR_MAX_TIME_CODERS,ADSR_MAX_TIME_CODERS,ADSR_MAX_TIME_CODERS,ADSR_MAX_TIME_CODERS,ADSR_MAX_LEVEL_CODERS};
uint16_t* adsrVar[CODER_NB];                        // inits dans menu.cpp ; les éléments inutilisés sont nullptr 

int main() {

    stdio_init_all();
    sleep_ms(2000);
    printf("\n+boumboum \n");    // %s\n",version);

    print_memory_report();

if (watchdog_caused_reboot()) {
    printf("RESET = WATCHDOG\n");
} else {
    printf("RESET = NORMAL\n");
}
    setup();
    init_test_7789(20,25*8,0,TFT_H-12*8,TFT_H,1);       // init screen animation

    i2s_start();                                        // launch sound output
printf("I2S DMA2 ctrl=%08x\n", dma_hw->ch[2].ctrl_trig);
printf("I2S DMA3 ctrl=%08x\n", dma_hw->ch[3].ctrl_trig);


    menus_init();

    while(1){
        uint8_t menu=coders_for_menu("boumboum ",(const char*)menu0_names,MENU0_NB,MENU_NAME_LEN,MENU0,menu0Coders,voicesSw,menu0MaxCoders,nullptr,0,MENU0_CODER_NB);

        switch(menu){
            //case VOICES_FR: currVoice=coders_for_freq(currVoice);break;
            case VOICES_FR: coders_for_menu("V",(const char*)nullptr,MAX_VOICES,0,VOICES,menuVcesCoders,voicesSw,menuMaxVcesCoders,vcesVar,VCE_VAR_NB,BASIC_WAVES_NB);break;
            case WAVES_AMP: coders_for_wavesAmpl(currVoice);break;
            //case GEN_AMPL_: coders_for_genAmpl(currVoice);break;
            case LFOS_____: coders_for_menu("L",(const char*)nullptr,MAX_LFO,0,LFOS,menuLfosCoders,voicesSw,menuMaxLfosCoders,lfosVar,LFO_VAR_NB,BASIC_WAVES_NB);break;
            case ADSRL____: coders_for_menu("A",(const char*)nullptr,MAX_LFO,0,ADSR,menuAdsrCoders,voicesSw,menuMaxAdsrCoders,adsrVar,ADSR_VAR_NB,BASIC_WAVES_NB);break;
            case MAPPING__: coders_for_mapping();break;

            default:break;
        }
    }
}
