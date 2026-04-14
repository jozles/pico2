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

extern const char menu0_names[][MENU_NAME_LEN];

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
        uint8_t menu=coders_for_menu((const char*)menu0_names,MENU0_NB,MENU_NAME_LEN);

        switch(menu){
            case VOICES_FR: currVoice=coders_for_freq(currVoice);break;
            case WAVES_AMP: coders_for_wavesAmpl(currVoice);break;
            case GEN_AMPL_: coders_for_genAmpl(currVoice);break;
            case LFOS_FREQ: coders_for_lfos_freq();break;
            case MAPPING__: coders_for_mapping();break;

            default:break;
        }
    }
}
