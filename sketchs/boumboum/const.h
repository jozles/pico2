#ifndef _CONST_H
#define _CONST_H_

#include "pico/stdlib.h"
#include "util.h"

#define PI 3.141592636

#define BB_TEST_MODE

/* spin_locks */

#define DMA_LOCK 2
#define WS_LOCK  3

/* coder */

#define MUXED_CODER

#define CODER_GPIO_CLOCK       10          
#define CODER_GPIO_DATA        11          
#define CODER_GPIO_SW          12
#define CODER_GPIO_VCC         13

#ifdef MUXED_CODER

#define CODER_BANK_NB           6
#define CODER_SEL_NB            4
#define CODER_NB                8
#define CODER_PIO_SEL0          6       // CODER_NB consecutive pins

// *** coders actions ***

#define SIN_LEV                 2       // sinus coder
#define SQR_LEV                 3       // sqare coder
#define TRI_LEV                 4       // triangle level
#define SAW_LEV                 5       // sawtooth level
#define WHI_LEV                 6       // white noise level
#define PIN_LEV                 7       // pink noise level

#define CODER_FREQUENCY         0       // voice frequency coder in all functions 

#define ATTACK                  2       // attack duration 
#define DECAY                   3       // decay duration 
#define SUSTAIN                 4       // sustain duration
#define RELEASE                 5       // realease duration
#define SUSTAIN_LEVEL           6       // sustain level

#endif // MUXED_CODER

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

#define CODER_TIMER_POOLING_INTERVAL_MS 1  // timer pooling interval in milliseconds            
#define CODER_STROBE_NUMBER 3              // minimal timer intervals for a valid new change

#define LEDONDURAT 20
#define LEDOFFDURAT 2000

/* Voices */

#ifdef MUXED_CODER
#define VOICES_NB CODER_NB

/* fonc names */
#define VOICES_MIXER 0              // VOICES_NB 
#define VOICE0_SPECTRUM_MIXER 1
#define VOICE1_SPECTRUM_MIXER 2
#define VOICE2_SPECTRUM_MIXER 3
#define VOICE3_SPECTRUM_MIXER 4
#define VOICE4_SPECTRUM_MIXER 5
#define VOICE5_SPECTRUM_MIXER 6
#define VOICE0_ADSR 7
#define VOICE1_ADSR 8
#define VOICE2_ADSR 9 
#define VOICE3_ADSR 10
#define VOICE4_ADSR 11 
#define VOICE5_ADSR 12
#define MAX_FONC 12

#define CODER_COUNTERS MAX_FONC*CODER_NB
#endif // MUXED_CODER

#ifndef MUXED_CODER
#define CODER_FREQUENCY 0
#define VOICES_NB 1
#define CODER_NB  1
#define CODER_BANK_NB  1
#define CODER_COUNTERS 1
#endif  // MUXED_CODER

/* Test_Pin */

#define TEST_PIN 22

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

//#define PICO_AUDIO_I2S_PIO 0   dans pico_audio_i2s.h
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 4 // 16        // 2 consecutive gpios
#define PICO_AUDIO_I2S_DATA_PIN 2 // 18

#define SAMPLE_RATE 44100
#define AMPLITUDE   30000   // Amplitude max (16 bits signé)
#define MAX_16B_LINEAR_VALUE 32 // 0 to 31 => 0,1,1.414,2,2.828,4,5.656,8,11.312,16,22.624 ... 8192,11583,16384,23167,32768,46334
// #define MAX_16B_LINEAR_VALUE 64 // 0 to 63 => 0,1,1.189,1.414,3.234,4,4.757,5.656,6.727,8 ....

//#define I2S_DATA_PIN  4     // DIN du MAX98357A
//#define I2S_BCLK_PIN  2     // BCLK
//#define I2S_LRCLK_PIN 3     // LRCLK

/* frequencies/voices */

#define SAMPLE_F SAMPLE_RATE        // fréquence d'échantillonnage audio   
#define SAMPLE_PER (float)1/SAMPLE_F
//#define FREQUENCY_DECIM 1000      // pour travailler en milliHz
#define NUMBER_OF_OCTAVES 10
#define OCTAVE0_FREQ SAMPLE_F/SAMPLE_BUFFER_SIZE
#define SAMPLES_PER_BUFFER 1024 //1156     // Samples / channel   
#define SAMPLE_BUFFER_SIZE SAMPLES_PER_BUFFER  // nombre d'échantillons par buffer (doit être multiple de 4 pour le dma i2s)

#define BASIC_WAVE_TABLE_POW 11     // ***** POWER OF 2 *****  nombre d'échantillons dans les tables d'ondes
#define BASIC_WAVE_TABLE_LEN 2048   // ***** POWER OF 2 *****  nombre d'échantillons dans les tables d'ondes
//#define SINE_WAVE_TABLE_POW 11    // ***** POWER OF 2 *****  nombre d'échantillons dans la table d'onde sinus
//#define SINE_WAVE_TABLE_LEN 2048  // ***** POWER OF 2 *****  nombre d'échantillons dans la table d'onde sinus

//#define VOICES_NB 4                 // nombre de voix simultanées

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

#endif  //_CONST_H_

