#ifndef _CONST_H_
#define _CONST_H_

#include "pico/stdlib.h"
#include "util.h"

#include "rc_33tables.h"

#define VERSION "v1.3y"

#define PI 3.141592636

#define BB_TEST_MODE

#define TST_PIN 0

#define BUT_VCC_PIN 28
#define BUTTON_PIN  27

/* slices */

#define PWM_IRQ_SLICE 0

/* spin_locks */

#define DMA_LOCK 5
#define WS_LOCK  3
#define CTL_INPUTS_LOCK 7

/* */

/* coders */

#define CODER_GPIO_CLOCK       10          
#define CODER_GPIO_DATA        11          
#define CODER_GPIO_SW          12
#define CODER_GPIO_VCC         22

#define CODER_NB                8
#define CODER_BANK_NB           6
#define CODER_SEL_NB            4       // bits to sel
#define CODER_PIO_SEL0          2       // CODER_SEL_NB consecutive pins

// *** coders actions ***

#define CODER_FREQUENCY         0       // voice frequency coder in all functions 

#define LOW 0
#define OFF 0
#define HIGH 1
#define ON 1

// these 2 parameters are working together ; their product is the minimum time where no change should occur 
// on the clock line after a change in mS
// the pooling interval duration is the time between 2 calls to the coder timer handler
// it's also the delay where no change should occur on the clock line after a change to validate it
// shortly there's 2 strobes : no change after change and no change before next change
// It's possible to reach 2mS between 2 changes but who cares

#define CODER_TIMER_POOLING_INTERVAL_MS 4  // timer pooling interval in milliseconds            
#define CODER_STROBE_NUMBER 3              // minimal timer intervals for a valid new change

#define CODER_SW_STROBE_MS 50              // minimal delay for switch valid change (debouncer);

/* Menu */

#define MENU_NAME_LEN 12
typedef enum {
#define Z(name,text) name,    
#include "menu.def"
        MENU0_NB
#undef Z
}  Menu;

/* Voices */

#define MAX_VOICES 4 
#define VCES_MAX_FREQ_CODERS 10000
#define VCES_MIN_FREQ_CODERS 25

/* lfos */

#define MAX_LFO 4
#define LFOS_SAMPLE_RATE 40
#define LFOS_MAX_FREQ_CODERS 3000
#define LFOS_MIN_FREQ_CODERS 420        // 30sec
#define OSC_SCOPE_BUFFER_LEN 256        // SAMPLE_BUFFER_SIZE         // OSC_SCOPE
#define VOICE_FREQ_DIVIDER  1024        // pour usage conjoint de calcFreq()

/* Adsrl */

#define MAX_ADSR 4
#define ADSR_MAX_TIME_CODERS 128
#define ADSR_MAX_LEVEL_CODERS MAX_16B_LINEAR_VALUE-1

/* led */

#define LEDBLINK  if((millisCounter-ledBlinker)>durOffOn[led]){ledBlinker=millisCounter;led=!led;gpio_put(LED,led);}
#define LEDBLINK_ERROR      durOffOn[0]=2*durOffOn[1];LEDBLINK
#define LEDBLINK_ERROR_DMA  durOffOn[0]=2*durOffOn[1];while(1){gpio_put(LED,led);sleep_ms(durOffOn[led]);gpio_put(LED,!led);sleep_ms(durOffOn[!led]);}
#define LEDONDUR 60
#define LEDOFFDUR 1000

#define LED 25              // pico2 built_in
#define LED_PIN LED

/* dma */

#define GLOBAL_DMA_IRQ_HANDLER

/* Ws2812 */

#define PICO_WS2812_PIO 1
#define ws2812_pio __CONCAT(pio, PICO_WS2812_PIO)
#define WS2812_LED_PIN 1    // until 4 wS2812 ledschains on pico2 with the same pio 
                            // each one using a different state machine (return from ledsWs2812Setup())
                            // voir commentaire dans main
#define WS2812_DREQ_PIO __CONCAT(DREQ_PIO,PICO_WS2812_PIO)
#define WS2812_DREQ_PIO_TX0 __CONCAT(WS2812_DREQ_PIO,_TX0)

/* I2S */

#define PICO_I2S_PIO 0
#define _i2s_pio __CONCAT(pio, PICO_I2S_PIO)   
#define PICO_AUDIO_I2S_DATA_PIN 13                      // 2 consecutive gpios
#define SAMPLE_RATE 44100
#define MAX_16B_LINEAR_VALUE 32                         // 0 to 31 => 0,1,1.414,2,2.828,4,5.656,8,11.312,16,22.624 ... 8192,11583,16384,23167,32768,46334
#define MIN_16B_LINEAR_VALUE 0

/* frequencies/voices */

#define SAMPLE_F SAMPLE_RATE                            // fréquence d'échantillonnage audio   
#define SAMPLE_PER (float)1/SAMPLE_F

#define NUMBER_OF_OCTAVES 10
#define OCTAVE0_FREQ SAMPLE_F/SAMPLE_BUFFER_SIZE
#define SAMPLES_PER_BUFFER 512                          // nombre d'échantillons (L+R) par buffer (1024 trop lent)  
#define SAMPLE_BUFFER_SIZE SAMPLES_PER_BUFFER           // taille du buffer (doit être multiple de 4 pour le dma i2s)

#define RC_TABLES_LEN RC_N_SAMPLES                      // ***** POWER OF 2 *****  nombre d'échantillons dans les 1/2 tables d'ondes
#define RC_TABLES_NB RC_N_TABLES                        // nombre de tables RC (MAXCODER_RC possible values)
#define CODER_CR_OFFSET RC_TABLES_NB
#define MAXCODER_RC (RC_TABLES_NB-1)*2                  // DOIT ETRE PAIR (-31 0 +31 : 63 values 0-62 ) le nombre total de tables doit être impair pour le mirroring
#define MINCODER_RC 0

#define BASIC_WAVE_TABLE_POW 11                         // ***** POWER OF 2 *****  nombre d'échantillons dans les tables d'ondes
#define BASIC_WAVE_TABLE_LEN RC_TABLES_LEN*2            // ***** POWER OF 2 *****  nombre d'échantillons dans les tables d'ondes

#define BASIC_WAVES_NB 6 // sinus, carré, triangle, dent de scie, bruit blanc,bruit rose 

/* inputs/outputs */

#define MAX_OUTPUT_OBJ          48
#define MAX_OUTPUTS_PER_OBJ     8
#define MAX_OUTPUTS             MAX_OUTPUT_OBJ*MAX_OUTPUTS_PER_OBJ
#define MAX_INPUT_OBJ           100
#define MAX_INPUTS_PER_OBJ      10
#define MAX_INPUTS              MAX_INPUT_OBJ*MAX_INPUTS_PER_OBJ
#define NO_LINK                 (-1)
#define OBJ_IO_NAME_LEN         5
#define IN_OUT_NAME_LEN         10

#define MAX_CTL_ATT             256

#define NO_ATTENUATION_VALUE    0x7fff
#define FULL_ATTENUATION_VALUE  0x0000

// ****** inputs ******

typedef enum {
#define X(name,text) name,    
#include "vces_inputs_names.def"
        VOICES_INPUTS_NB  
#undef X
}  Vces_inputs_names;

typedef enum {
#define X(name,text) name,    
#include "lfos_inputs_names.def"
        LFO_INPUTS_NB  
#undef X
}  Lfo_inputs_names;

typedef enum {
#define X(name,text) name,    
#include "adsr_inputs_names.def"
        ADSR_INPUTS_NB  
#undef X
}  Adsr_inputs_names;

typedef enum {
#define X(name,text) name,    
#include "norm_types.def"
        NORM_TYPES_NB  
#undef X
}  Norm_types_names;

// ****** outputs ******

typedef enum {
#define X(name,text) name,    
#include "lfos_outputs_names.def"
        LFO_OUTPUTS_NB  
#undef X
}  Lfo_outputs_names;

typedef enum {
#define X(name,text) name,    
#include "adsr_outputs_names.def"
        ADSR_OUTPUTS_NB  
#undef X
}  Adsr_outputs_names;

typedef enum {
#define X(name,text) name,    
#include "vces_outputs_names.def"
        VCES_OUTPUTS_NB  
#undef X
}  Vces_outputs_names;

// ****** menus codes ******

enum Menus {
    MENU0,
    VOICES,
    LFOS,
    ADSR,
    AMPS,
    MENUS_NB
};

// ****** basics waves codes ******

#define FIRST_WAVE W_SINUS
#define LAST_WAVE W_SQUARE
enum Waves {
    W_SINUS,
    W_TRIANGLE,
    W_SAWTOOTH,
    W_SQUARE,
    W_WHITE_NOISE,
    W_PINK_NOISE,
    W_NB,
    W_TEST
};    

#define PIN_DCDC_PSM_CTRL 23        // to set the DCDC in PSM mode for less audio noise

/* st7789 */

#define TFT_W 240
#define TFT_H 240

// le dma utilise l'irq1 ; l'irq 0 est utilisée par le dma de l'i2s

#define ST7789_PIN_SCK   18
#define ST7789_PIN_MOSI  19
#define ST7789_PIN_DC    16
#define ST7789_PIN_RST   20
#define ST7789_PIN_CS    17
#define ST7789_PIN_BL    21

#define ST7789_SPI 0
#define ST7789_spi __CONCAT(spi, ST7789_SPI)
#define ST7789_SPI_SPEED 40000000

#endif  // _CONST_H_

