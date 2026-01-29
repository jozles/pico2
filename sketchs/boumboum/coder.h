#ifndef CODER_H_
#define CODER_H_



// these 2 parameters are working together ; their product is the minimum time where no change should occur 
// on the clock line after a change
// the pooling interval duration is the time between 2 calls to the coder timer handler
// its also half the time where no change should occur on the clock line after a change 
// to validate it
// shortly there's 2 strobes : no change after change and no change before next change

//#define CODER_TIMER_POOLING_INTERVAL_MS 5  // timer pooling interval in milliseconds
// #define CODER_STROBE_NUMBER 3              // number of timer intervals for a valid strobe

#ifndef MUXED_CODER
void coderInit(uint8_t pio_ck,uint8_t pio_d,uint8_t pio_sw,uint16_t ctpi,uint8_t cstn);
bool coderTimerHandler();
#endif // MUXED_CODER
#ifdef MUXED_CODER

void coderInit(Coders* c,uint8_t ck,uint8_t data,uint8_t sw,uint8_t sel0,uint8_t sel_nb,uint8_t nb,uint16_t ctpi,uint8_t cstn);
bool coderTimerHandler(Coders* c);
struct Coders{
    uint8_t coderItStatus[CODER_NB];                    // coder decoding status
    bool coderClock[CODER_NB];                  // current physical coder clock value
    bool coderClock0[CODER_NB];                 // previous physical coder clock value
    bool coderData[CODER_NB];                   // current physical coder data value
    bool coderData0[CODER_NB];                  // previous physical coder data value
    bool coderSwitch[CODER_NB];                 // current physical coder switch value
}
#endif // MUXED_CODER
void coderSetup(volatile int32_t* cTC);



#endif /* CODER_H_ */
