#ifndef _CONST_H
#define _CONST_H_

#include "pico/stdlib.h"
#include "util.h"

#define PI 3.141592636

/* coder */

#define PIO_CLOCK         10          
#define PIO_DATA          11          
#define PIO_SW            12          
#define PIO_VPP           13    // vcc coder                         

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

/* Test_Pin */

#define TEST_PIN 22

/* led */

#define LEDBLINK  if((millisCounter-ledBlinker)>durOffOn[led]){ledBlinker=millisCounter;led=!led;gpio_put(LED,led);}
#define LEDBLINK_ERROR      durOffOn[0]=2*durOffOn[1];LEDBLINK
//#define LEDBLINK_ERROR_DMA  durOffOn[0]=3*durOffOn[1];LEDBLINK
#define LEDONDUR 60
#define LEDOFFDUR 1000

#define LED 25  // pico2 built_in
#define LED_PIN LED

/* Ws2812 */

#define PICO_WS2812_PIO 1
#define ws2812_pio __CONCAT(pio, PICO_WS2812_PIO)
#define WS2812_LED_PIN_0 0  // until 4 wS2812 ledschains on pico2 with the same pio 
                            // each one using a different state machine (return from ledsWs2812Setup())

/* I2S */

//#define PICO_AUDIO_I2S_PIO 0   dans pico_audio_i2s.h
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 4 // 16        // 2 consecutive gpios
#define PICO_AUDIO_I2S_DATA_PIN 2 // 18

#define SAMPLE_RATE 44100
#define FREQUENCY   440.0   // La 440 Hz
#define AMPLITUDE   30000   // Amplitude max (16 bits signé)

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

#define BASIC_WAVE_TABLE_POW 11      // ***** POWER OF 2 *****  nombre d'échantillons dans les tables d'ondes
#define BASIC_WAVE_TABLE_LEN 2048   // ***** POWER OF 2 *****  nombre d'échantillons dans les tables d'ondes
//#define SINE_WAVE_TABLE_POW 11      // ***** POWER OF 2 *****  nombre d'échantillons dans la table d'onde sinus
//#define SINE_WAVE_TABLE_LEN 2048    // ***** POWER OF 2 *****  nombre d'échantillons dans la table d'onde sinus

#define VOICES_NB 4                 // nombre de voix simultanées

#define PIN_DCDC_PSM_CTRL 23        // to set the DCDC in PSM mode for less audio noise

/* st7789 */

#define ST7789_PIN_SCK   18
#define ST7789_PIN_MOSI  19
#define ST7789_PIN_DC    16
#define ST7789_PIN_RST   20
#define ST7789_PIN_CS    17

#define ST7789_SPI 0
#define ST7789_spi __CONCAT(spi, ST7789_SPI)

#endif  //_CONST_H_

