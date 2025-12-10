#ifndef _CONST_H
#define _CONST_H_

#include "pico/stdlib.h"

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

#define LED 25  // pico2 built_in

#define NBLEDS            3
#define LED_BLUE          47
#define LED_RED           53
#define LED_GREEN         49

/* I2S */

#define SAMPLE_RATE 44100
#define FREQUENCY   440.0   // La 440 Hz
#define AMPLITUDE   30000   // Amplitude max (16 bits signé)

#define I2S_DATA_PIN  4     // DIN du MAX98357A
#define I2S_BCLK_PIN  2     // BCLK
#define I2S_LRCLK_PIN 3     // LRCLK

/* oscillator */

#define WFSTEPNB 2048
#define SAMPLE_F SAMPLE_RATE       
#define SAMPLE_PER (float)1/SAMPLE_F

static const uint32_t PIN_DCDC_PSM_CTRL = 23;

#endif  //_CONST_H_

