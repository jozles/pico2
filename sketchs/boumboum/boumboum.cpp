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

uint8_t currVoice=0;



int main() {

    stdio_init_all();

    sleep_ms(1000);

    gpio_init(TST_PIN);gpio_set_dir(TST_PIN,GPIO_OUT); gpio_put(TST_PIN,LOW);    
    gpio_init(LED);gpio_set_dir(LED,GPIO_OUT); gpio_put(LED,LOW);
    delayBlk(3);        
    printf("\n+boumboum= \n");
    
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
        uint8_t menu=coders_for_menu();

        switch(menu){
            case VOICES_FR: currVoice=coders_for_freq(currVoice);break;
            case WAVES_AMP: coders_for_wavesAmpl(currVoice);break;
            case GEN_AMPL_: coders_for_genAmpl(currVoice);break;
            case LFOS_FREQ: currVoice=coders_for_lfos_freq(currVoice);break;
            case MAPPING__: coders_for_mapping();break;

            default:break;
        }
    }
}
