#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "const.h"
#include "coder.h"
#include "util.h"
#include "std_utils.h"
#include "bb_i2s.h"
#include "frequences.h"
#include "leds.h"
#include "st7789.h"
#include "menus.h"
#include "sound_level_management.h"
#include "tests.h"

const char* version=VERSION;
uint8_t currVoice=0;

extern volatile bool codersSw[];                   // coder it handler scans all physical coders
extern volatile bool codersTB[CODER_NB];

// RULE: the coder interrupt reads and writes the CODER_NB physical coders whatever the menu displayed:
// every table given to coderSetup() (current values and maxima) must have CODER_NB elements ; the maximum of an unused coder is 0

#define MENU0_CODER_NB 1
volatile int16_t menu0Coders[CODER_NB]={0};         // [0] curr input nb
uint16_t menu0MaxCoders[CODER_NB]={MENU0_NB-1};

extern const char menu0_names[][MENU_NAME_LEN];

// xxxx noms des objets (vces,lfos,adsr)
// yyyy noms des fonctions attribuées aux coders pour un objet
// xxxxVar[CODER_NB] sont les pointeurs sur le tableau des valeurs des coders de chaque type d'objet dans coders_for_menu
// essentiellement utilisé pour permettre un peu de paramétrage dans coders_for_menu
// les tableaux des valeurs de coders sont de la forme xxxxCodersyyyy[nbre maxi d'objets] 
// voir les commentaires de coders_for_menu

#define VCE_VAR_F_NB (1+5)                              // voices frequencies menu
volatile int16_t menuVcesCodersF[CODER_NB];             // [0] curr input nb ; [1] freq ; [2] rc ; [3] freq att ; [4] rc att ; [6] gain coder
uint16_t menuMaxVcesCodersF[CODER_NB]={MAX_VOICES-1,VCES_MAX_FREQ_CODERS,MAXCODER_RC,MAX_CTL_ATT,MAX_CTL_ATT,0,MAX_16B_LINEAR_VALUE-1,0};
uint16_t* vcesVarF[CODER_NB];                           // inits dans menu.cpp ; les éléments inutilisés sont nullptr 

#define VCE_VAR_M_NB (1+6+1)                            // voices ampl menu : 6 waves
volatile int16_t menuVcesCodersM[CODER_NB];             // [0] curr input nb ; [1] sin ; [2] tri etc (saw,sqr,wh,pnk)
uint16_t menuMaxVcesCodersM[CODER_NB]={MAX_VOICES-1,WAVE_LEVEL_STEPS-1,WAVE_LEVEL_STEPS-1,WAVE_LEVEL_STEPS-1,WAVE_LEVEL_STEPS-1,WAVE_LEVEL_STEPS-1,WAVE_LEVEL_STEPS-1};
uint16_t* vcesVarM[CODER_NB];                           // inits dans menu.cpp ; les éléments inutilisés sont nullptr 

#define VCE_VAR_E_NB (1+4)                              // voices effects menu : echo + echo att + gen + gen att
volatile int16_t menuVcesCodersE[CODER_NB];             // [0] curr input nb ; [1] echo ; [2] echo_att ; [3] gen ; [4] gen_att
uint16_t menuMaxVcesCodersE[CODER_NB]={MAX_VOICES-1,WAVE_LEVEL_STEPS-1,MAX_CTL_ATT,WAVE_LEVEL_STEPS-1,MAX_CTL_ATT};
uint16_t* vcesVarE[CODER_NB];                           // inits dans menu.cpp ; les éléments inutilisés sont nullptr

#define VCE_VAR_T_NB (1+6)                              // voices attenuators menu
volatile int16_t menuVcesCodersT[CODER_NB];             // [0] curr input nb ; [1] sin att ; [2] tri att etc (saw,sqr,wh,pnk)
uint16_t menuMaxVcesCodersT[CODER_NB]={MAX_VOICES-1,MAX_CTL_ATT,MAX_CTL_ATT,MAX_CTL_ATT,MAX_CTL_ATT,MAX_CTL_ATT,MAX_CTL_ATT,MAX_CTL_ATT};
uint16_t* vcesVarT[CODER_NB];                           // inits dans menu.cpp ; les éléments inutilisés sont nullptr 

#define VCE_VAR_FI_NB (1+5)                             // voices filter menu
volatile int16_t menuVcesCodersFi[CODER_NB];            // [0] curr input nb ; [1] freq ; [2] freq att ; [3] inp att ; [4] res ; [5] res att
uint16_t menuMaxVcesCodersFi[CODER_NB]={MAX_VOICES-1,VCES_MAX_FREQ_CODERS,MAX_CTL_ATT-1,MAX_CTL_ATT-1,MAX_CTL_ATT-1,MAX_CTL_ATT-1,0,0};
uint16_t* vcesVarFi[CODER_NB];                          // inits dans menu.cpp ; les éléments inutilisés sont nullptr 

#define LFO_VAR_NB (1+4)
volatile int16_t menuLfosCoders[CODER_NB];              // [0] curr input nb ; [1] freq ; [2] cr ; [3] att freq ; [4] att cr
uint16_t menuMaxLfosCoders[CODER_NB]={MAX_LFO-1,LFOS_MAX_FREQ_CODERS,MAXCODER_RC,MAX_CTL_ATT,MAX_CTL_ATT,0,0,0};
uint16_t* lfosVar[CODER_NB];                            // inits dans menu.cpp ; les éléments inutilisés sont nullptr                

#define ADSR_VAR_NB (1+5)
volatile int16_t menuAdsrCoders[CODER_NB];              // [0] curr input nb ; [1] Att ; [2] Dec ; [3] Sus ; [4] Rel ; [5] Lev
uint16_t menuMaxAdsrCoders[CODER_NB]={MAX_ADSR-1,ADSR_MAX_TIME_CODERS,ADSR_MAX_TIME_CODERS,ADSR_MAX_TIME_CODERS,ADSR_MAX_TIME_CODERS,ADSR_MAX_LEVEL_CODERS,0,0};
uint16_t* adsrVar[CODER_NB];                            // inits dans menu.cpp ; les éléments inutilisés sont nullptr 

#define LFM_VAR_NB (1+4)                         
volatile int16_t menuLfmCoders[CODER_NB];               // [0] curr input nb ; [1] inp0 ; [2] inp0 att ; [3] inp1 ; [4] inp1 att
uint16_t menuMaxLfmCoders[]={MAX_LFM-1,MAX_CTL_ATT-1,MAX_CTL_ATT-1,MAX_CTL_ATT-1,MAX_CTL_ATT-1,MAX_CTL_ATT-1,MAX_CTL_ATT-1,MAX_CTL_ATT-1};
uint16_t* lfmVar[CODER_NB];                             // inits dans menu.cpp ; les éléments inutilisés sont nullptr

#define ECH_VAR_NB (1+6)
volatile int16_t menuEchoCoders[CODER_NB];              // [0] curr input nb ; [1] dur ; [2] dur ret ; [3] inj ; [4] inj ret ; [5] ret ; [6] att ret
uint16_t menuMaxEchoCoders[CODER_NB]={MAX_ECHO-1,DUR_ECH_NB-1,MAX_CTL_ATT,ECHO_INJ_STEPS-1,MAX_CTL_ATT,WAVE_LEVEL_STEPS-1,MAX_CTL_ATT};
uint16_t* echoVar[CODER_NB];                            // inits dans menu.cpp ; les éléments inutilisés sont nullptr 

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

    //testVoiceInit(3,0);

    menus_init();
    uint8_t menu=1;

   

    while(1){

        menu=coders_for_menu("boumboum ",(const char*)menu0_names,MENU0_NB,MENU_NAME_LEN,MENU0____,menu0Coders,codersSw,codersTB,menu0MaxCoders,nullptr,0,MENU0_CODER_NB,menu);

        switch(menu){
            case VOICES_FR: coders_for_menu("V",(const char*)nullptr,MAX_VOICES,0,VOICES_FR,menuVcesCodersF,codersSw,codersTB,menuMaxVcesCodersF,vcesVarF,VCE_VAR_F_NB,BASIC_WAVES_NB,0);break;
            case VOICES_AM: coders_for_menu("V_AMP",(const char*)nullptr,MAX_VOICES,0,VOICES_AM,menuVcesCodersM,codersSw,codersTB,menuMaxVcesCodersM,vcesVarM,VCE_VAR_M_NB,BASIC_WAVES_NB,0);break;
            case VOICES_AT: coders_for_menu("V_ATT",(const char*)nullptr,MAX_VOICES,0,VOICES_AT,menuVcesCodersT,codersSw,codersTB,menuMaxVcesCodersT,vcesVarT,VCE_VAR_T_NB,BASIC_WAVES_NB,0);break;
            case VOICES_FI: coders_for_menu("V_FI",(const char*)nullptr,MAX_VOICES,0,VOICES_FI,menuVcesCodersFi,codersSw,codersTB,menuMaxVcesCodersFi,vcesVarFi,VCE_VAR_FI_NB,BASIC_WAVES_NB,0);break;
            case VOICES_EF: coders_for_menu("V_EFF",(const char*)nullptr,MAX_VOICES,0,VOICES_EF,menuVcesCodersE,codersSw,codersTB,menuMaxVcesCodersE,vcesVarE,VCE_VAR_E_NB,BASIC_WAVES_NB,0);break;
            //case WAVES_AMP: coders_for_wavesAmpl(currVoice);break;
            //case GEN_AMPL_: coders_for_genAmpl(currVoice);break;
            case LFOS_____: coders_for_menu("L",(const char*)nullptr,MAX_LFO,0,LFOS_____,menuLfosCoders,codersSw,codersTB,menuMaxLfosCoders,lfosVar,LFO_VAR_NB,BASIC_WAVES_NB,0);break;
            case ADSRL____: coders_for_menu("ADSR:",(const char*)nullptr,MAX_ADSR,0,ADSRL____,menuAdsrCoders,codersSw,codersTB,menuMaxAdsrCoders,adsrVar,ADSR_VAR_NB,BASIC_WAVES_NB,0);break;
            case LMUX_____: coders_for_menu("LM",(const char*)nullptr,MAX_LFM,0,LMUX_____,menuLfmCoders,codersSw,codersTB,menuMaxLfmCoders,lfmVar,LFM_VAR_NB,LFM_INPUTS_NB,0);break;            
            case ECHOs____: coders_for_menu("ECHOs",(const char*)nullptr,MAX_ECHO,0,ECHOs____,menuEchoCoders,codersSw,codersTB,menuMaxEchoCoders,echoVar,ECH_VAR_NB,ECHO_INPUTS_NB,0);break;            
            case MAPPING__: coders_for_mapping();break;

            default:break;
        }
    }
}
