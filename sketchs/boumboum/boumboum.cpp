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

volatile bool codersSw[CODER_NB];                   // coder it handler scans all physical coders
volatile bool codersTB[CODER_NB];

#define MENU0_CODER_NB 1
volatile int16_t menu0Coders[]={0};                 // [0] curr input nb
uint16_t menu0MaxCoders[]={MENU0_NB-1};

extern const char menu0_names[][MENU_NAME_LEN];

// xxxx noms des objets (vces,lfos,adsr)
// yyyy noms des fonctions attribuées aux coders pour un objet
// xxxxVar[CODER_NB] sont les pointeurs sur le tableau des valeurs des coders de chaque type d'objet dans coders_for_menu
// essentiellement utilisé pour permettre un peu de paramétrage dans coders_for_menu
// les tableaux des valeurs de coders sont de la forme xxxxCodersyyyy[nbre maxi d'objets] 
// voir les commentaires de coders_for_menu

#define VCE_VAR_F_NB 6                                // voices frequencies menu
volatile int16_t menuVcesCodersF[VCE_VAR_F_NB+1];     // [0] curr input nb ; [1] curr coder value for freq ; [2] curr coder value for rc ; [3] curr coder value for genAmpl
uint16_t menuMaxVcesCodersF[]={MAX_VOICES-1,VCES_MAX_FREQ_CODERS,MAXCODER_RC,MAX_CTL_ATT,MAX_CTL_ATT,MAX_16B_LINEAR_VALUE-1};
uint16_t* vcesVarF[CODER_NB];                        // inits dans menu.cpp ; les éléments inutilisés sont nullptr 

#define VCE_VAR_M_NB 8                                // voices ampl menu
volatile int16_t menuVcesCodersM[VCE_VAR_M_NB+1];     // [0] curr input nb ; [1] curr coder value for sin ; [2] curr coder value for tri etc (saw,sqr,wh,pnk)
uint16_t menuMaxVcesCodersM[]={MAX_VOICES-1,MAX_16B_LINEAR_VALUE-1,MAX_16B_LINEAR_VALUE-1,MAX_16B_LINEAR_VALUE-1,MAX_16B_LINEAR_VALUE-1,MAX_16B_LINEAR_VALUE-1,MAX_16B_LINEAR_VALUE-1,MAX_16B_LINEAR_VALUE-1};
uint16_t* vcesVarM[CODER_NB];                        // inits dans menu.cpp ; les éléments inutilisés sont nullptr 

#define VCE_VAR_T_NB 8                                // voices attenuators menu
volatile int16_t menuVcesCodersT[VCE_VAR_T_NB+1];     // [0] curr input nb ; [1] curr coder value for sin att ; [2] curr coder value for tri att etc (saw,sqr,wh,pnk)
uint16_t menuMaxVcesCodersT[]={MAX_VOICES-1,MAX_CTL_ATT,MAX_CTL_ATT,MAX_CTL_ATT,MAX_CTL_ATT,MAX_CTL_ATT,MAX_CTL_ATT,MAX_CTL_ATT};
uint16_t* vcesVarT[CODER_NB];                        // inits dans menu.cpp ; les éléments inutilisés sont nullptr 

#define LFO_VAR_NB 5
volatile int16_t menuLfosCoders[LFO_VAR_NB+1];      // [0] curr input nb ; [1] curr coder value freq ; [2] curr coder value cr ; [3] att value freq ; [4] att value cr
uint16_t menuMaxLfosCoders[]={MAX_LFO-1,LFOS_MAX_FREQ_CODERS,MAXCODER_RC,MAX_CTL_ATT,MAX_CTL_ATT};
uint16_t* lfosVar[CODER_NB];                        // inits dans menu.cpp ; les éléments inutilisés sont nullptr                

#define ADSR_VAR_NB 6
volatile int16_t menuAdsrCoders[ADSR_VAR_NB+1];     // [0] curr input nb ; [1] curr coder value for Att ; [2] curr coder value for Dec ; [3] curr coder value for Sus ; [4] curr coder value for Rel ; [5] curr coder value for Lev
uint16_t menuMaxAdsrCoders[]={MAX_ADSR-1,ADSR_MAX_TIME_CODERS,ADSR_MAX_TIME_CODERS,ADSR_MAX_TIME_CODERS,ADSR_MAX_TIME_CODERS,ADSR_MAX_LEVEL_CODERS};
uint16_t* adsrVar[CODER_NB];                        // inits dans menu.cpp ; les éléments inutilisés sont nullptr 

int main() {

    stdio_init_all();
    sleep_ms(2000);
    printf("\n+boumboum %s\n",version);

    print_memory_report();

if (watchdog_caused_reboot()) {
    printf("RESET = WATCHDOG\n\n");
} else {
    printf("RESET = NORMAL\n\n");
}
    setup();
    init_test_7789(20,25*8,0,TFT_H-12*8,TFT_H,1);       // init screen animation

    i2s_start(true);                                    // launch sound output

    testSetup();

    menus_init();
    uint8_t menu=1;

    while(1){
//printf("menu:%d \n");
        menu=coders_for_menu("boumboum ",(const char*)menu0_names,MENU0_NB,MENU_NAME_LEN,MENU0____,menu0Coders,codersSw,codersTB,menu0MaxCoders,nullptr,0,MENU0_CODER_NB,menu);

        switch(menu){
            case VOICES_FR: coders_for_menu("V",(const char*)nullptr,MAX_VOICES,0,VOICES_FR,menuVcesCodersF,codersSw,codersTB,menuMaxVcesCodersF,vcesVarF,VCE_VAR_F_NB,BASIC_WAVES_NB,0);break;
            case VOICES_AM: coders_for_menu("V_AMP",(const char*)nullptr,MAX_VOICES,0,VOICES_AM,menuVcesCodersM,codersSw,codersTB,menuMaxVcesCodersM,vcesVarM,VCE_VAR_M_NB,BASIC_WAVES_NB,0);break;
            case VOICES_AT: coders_for_menu("V_ATT",(const char*)nullptr,MAX_VOICES,0,VOICES_AT,menuVcesCodersT,codersSw,codersTB,menuMaxVcesCodersT,vcesVarT,VCE_VAR_T_NB,BASIC_WAVES_NB,0);break;
            //case WAVES_AMP: coders_for_wavesAmpl(currVoice);break;
            //case GEN_AMPL_: coders_for_genAmpl(currVoice);break;
            case LFOS_____: coders_for_menu("L",(const char*)nullptr,MAX_LFO,0,LFOS_____,menuLfosCoders,codersSw,codersTB,menuMaxLfosCoders,lfosVar,LFO_VAR_NB,BASIC_WAVES_NB,0);break;
            case ADSRL____: coders_for_menu("ADSR:",(const char*)nullptr,MAX_ADSR,0,ADSRL____,menuAdsrCoders,codersSw,codersTB,menuMaxAdsrCoders,adsrVar,ADSR_VAR_NB,BASIC_WAVES_NB,0);break;
            case MAPPING__: coders_for_mapping();break;

            default:break;
        }
    }
}
