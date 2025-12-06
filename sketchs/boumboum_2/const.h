#ifndef _CONST_H
#define _CONST_H_


/* coder */

#define CODER_PORT PORTD

#define PIN_CODER_GND 23
#define PIN_CODER_VCC 24
#define PIN_CODER_A   27        // clock
#define PIN_CODER_B   25        // data
#define PIN_CODER_C   26        // switch

// every pins of a specific coder should be on the same port
#define PIO_PDSR_D uint32_t(*(volatile uint32_t *)0x400E143CU)   // PORTD 32 bits
#define PIO_CLOCK         2                              // D2 sur PORTD
#define PIO_DATA          0                              // D0 sur PORTD
#define PIO_SW            1                              // D1 sur PORTD

#define LOW 0
#define HIGH 1

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
#define LED_GND           51

/* oscillator */

#define WFSTEPNB 2048
#define SAMPLE_F 44100       
#define SAMPLE_PER (float)1/SAMPLE_F 

#endif  //_CONST_H_

